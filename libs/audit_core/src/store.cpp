#include "ingeneer/audit/store.hpp"

#include <sqlite3.h>

#include <optional>
#include <utility>

#include "ingeneer/audit/canonical.hpp"
#include "ingeneer/audit/sha256.hpp"

namespace ingeneer::audit {
namespace {

constexpr const char* kGenesis = "0000000000000000000000000000000000000000000000000000000000000000";

const char* const kSchema = R"sql(
PRAGMA journal_mode=WAL;
PRAGMA synchronous=FULL;
PRAGMA foreign_keys=ON;
PRAGMA busy_timeout=5000;

CREATE TABLE IF NOT EXISTS event (
    seq          INTEGER PRIMARY KEY,
    ts           TEXT    NOT NULL,
    chain_id     TEXT    NOT NULL,
    event_type   TEXT    NOT NULL,
    payload_json TEXT    NOT NULL,
    prev_hash    TEXT    NOT NULL,
    hash         TEXT    NOT NULL
);
CREATE UNIQUE INDEX IF NOT EXISTS ux_event_hash ON event(hash);

CREATE TABLE IF NOT EXISTS entity (
    entity_id          TEXT PRIMARY KEY,
    authority_class    INTEGER NOT NULL,
    source_agent       TEXT,
    created_by         TEXT NOT NULL,
    created_at         TEXT NOT NULL,
    approved_by        TEXT,
    approved_at        TEXT,
    confidence         REAL,
    verification_state TEXT NOT NULL,
    head_seq           INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS promotion (
    seq        INTEGER PRIMARY KEY,
    entity_id  TEXT NOT NULL,
    from_class INTEGER,
    to_class   INTEGER NOT NULL,
    actor      TEXT NOT NULL,
    actor_kind TEXT NOT NULL,
    reason     TEXT,
    FOREIGN KEY(seq) REFERENCES event(seq),
    FOREIGN KEY(entity_id) REFERENCES entity(entity_id)
);

CREATE TRIGGER IF NOT EXISTS no_update_event BEFORE UPDATE ON event
  BEGIN SELECT RAISE(ABORT, 'event is append-only'); END;
CREATE TRIGGER IF NOT EXISTS no_delete_event BEFORE DELETE ON event
  BEGIN SELECT RAISE(ABORT, 'event is append-only'); END;
CREATE TRIGGER IF NOT EXISTS no_update_promotion BEFORE UPDATE ON promotion
  BEGIN SELECT RAISE(ABORT, 'promotion is append-only'); END;
CREATE TRIGGER IF NOT EXISTS no_delete_promotion BEFORE DELETE ON promotion
  BEGIN SELECT RAISE(ABORT, 'promotion is append-only'); END;

CREATE VIEW IF NOT EXISTS certified_snapshot AS
  SELECT * FROM entity WHERE authority_class = 3;
)sql";

AuditError io(sqlite3* db, const char* what) {
    std::string msg = what;
    if (db != nullptr) {
        msg += ": ";
        msg += sqlite3_errmsg(db);
    }
    return AuditError(AuditErrc::Io, std::move(msg));
}

bool exec(sqlite3* db, const char* sql) {
    return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

// RAII prepared statement: prepare is checked (ok()), finalize is unconditional on scope
// exit. Eliminates both the NULL-stmt UB on ignored prepare failures and finalize leaks on
// early return (review CRIT-2 / MED-3).
class Stmt {
public:
    Stmt(sqlite3* db, const char* sql) { rc_ = sqlite3_prepare_v2(db, sql, -1, &st_, nullptr); }
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;
    ~Stmt() {
        if (st_ != nullptr) {
            sqlite3_finalize(st_);
        }
    }

    bool ok() const { return rc_ == SQLITE_OK && st_ != nullptr; }
    sqlite3_stmt* get() const { return st_; }

    void bind_text(int idx, const std::string& v) {
        sqlite3_bind_text(st_, idx, v.c_str(), -1, SQLITE_TRANSIENT);
    }
    void bind_static(int idx, const char* v) { sqlite3_bind_text(st_, idx, v, -1, SQLITE_STATIC); }
    void bind_int(int idx, int v) { sqlite3_bind_int(st_, idx, v); }
    void bind_int64(int idx, std::int64_t v) { sqlite3_bind_int64(st_, idx, v); }
    void bind_double(int idx, double v) { sqlite3_bind_double(st_, idx, v); }
    void bind_null(int idx) { sqlite3_bind_null(st_, idx); }

    int step() { return sqlite3_step(st_); }

private:
    sqlite3_stmt* st_ = nullptr;
    int rc_ = SQLITE_ERROR;
};

// NULL-safe TEXT column read (review CRIT-1): sqlite3_column_text can return NULL on a
// NULL value (constraint-bypassing corruption / adversarial file) or OOM; feeding that to
// std::string is UB. nullopt signals "column was NULL".
std::optional<std::string> col_text(sqlite3_stmt* st, int col) {
    const unsigned char* p = sqlite3_column_text(st, col);
    if (p == nullptr) {
        return std::nullopt;
    }
    return std::string(reinterpret_cast<const char*>(p));
}

// RAII transaction: BEGIN IMMEDIATE on construction; ROLLBACK on scope exit unless
// commit() succeeded. A failed COMMIT also rolls back so the connection is never left
// inside a dangling transaction (review CRIT-4).
class Txn {
public:
    explicit Txn(sqlite3* db) : db_(db) { open_ = exec(db_, "BEGIN IMMEDIATE"); }
    Txn(const Txn&) = delete;
    Txn& operator=(const Txn&) = delete;
    ~Txn() {
        if (open_) {
            exec(db_, "ROLLBACK");
        }
    }

    bool began() const { return open_; }
    bool commit() {
        if (!open_) {
            return false;
        }
        if (exec(db_, "COMMIT")) {
            open_ = false;
            return true;
        }
        exec(db_, "ROLLBACK");
        open_ = false;
        return false;
    }

private:
    sqlite3* db_;
    bool open_ = false;
};

// Append one event inside an already-open transaction. Returns the new head.
std::expected<AppendResult, AuditError> append_locked(sqlite3* db, const std::string& chain_id,
                                                      const Event& ev) {
    // Determine seq and prev_hash from the current head.
    std::int64_t seq = 1;
    std::string prev = kGenesis;
    {
        Stmt head(db, "SELECT seq, hash FROM event ORDER BY seq DESC LIMIT 1");
        if (!head.ok()) {
            return std::unexpected(io(db, "prepare head"));
        }
        if (head.step() == SQLITE_ROW) {
            seq = sqlite3_column_int64(head.get(), 0) + 1;
            auto h = col_text(head.get(), 1);
            if (!h) {
                return std::unexpected(
                    AuditError(AuditErrc::ChainBroken, "NULL head hash (corrupted database)"));
            }
            prev = std::move(*h);
        }
    }

    const std::string record =
        canonical_record(seq, ev.timestamp, chain_id, ev.event_type, ev.payload_json, prev);
    const std::string hash = sha256_hex(record);

    Stmt ins(db,
             "INSERT INTO event(seq,ts,chain_id,event_type,payload_json,prev_hash,hash)"
             " VALUES(?,?,?,?,?,?,?)");
    if (!ins.ok()) {
        return std::unexpected(io(db, "prepare insert event"));
    }
    ins.bind_int64(1, seq);
    ins.bind_text(2, ev.timestamp);
    ins.bind_text(3, chain_id);
    ins.bind_text(4, ev.event_type);
    ins.bind_text(5, ev.payload_json);
    ins.bind_text(6, prev);
    ins.bind_text(7, hash);
    if (ins.step() != SQLITE_DONE) {
        return std::unexpected(io(db, "insert event"));
    }

    return AppendResult{seq, hash};
}

// Insert one promotion row inside an open transaction.
std::expected<void, AuditError> insert_promotion(sqlite3* db, std::int64_t seq,
                                                 const std::string& entity_id,
                                                 std::optional<int> from_class, int to_class,
                                                 const std::string& actor, ActorKind kind,
                                                 const std::string& reason) {
    Stmt pr(db,
            "INSERT INTO promotion(seq,entity_id,from_class,to_class,actor,actor_kind,reason)"
            " VALUES(?,?,?,?,?,?,?)");
    if (!pr.ok()) {
        return std::unexpected(io(db, "prepare insert promotion"));
    }
    pr.bind_int64(1, seq);
    pr.bind_text(2, entity_id);
    if (from_class) {
        pr.bind_int(3, *from_class);
    } else {
        pr.bind_null(3);
    }
    pr.bind_int(4, to_class);
    pr.bind_text(5, actor);
    pr.bind_static(6, kind == ActorKind::Human ? "human" : "agent");
    pr.bind_text(7, reason);
    if (pr.step() != SQLITE_DONE) {
        return std::unexpected(io(db, "insert promotion"));
    }
    return {};
}

}  // namespace

std::expected<Store, AuditError> Store::open(const std::string& path, std::string chain_id) {
    if (chain_id.empty()) {
        return std::unexpected(AuditError(AuditErrc::InvalidArgument, "chain_id required"));
    }
    sqlite3* db = nullptr;
    // open_v2 pins the access mode explicitly (review HIGH-1).
    if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) !=
        SQLITE_OK) {
        AuditError e = io(db, "open");
        sqlite3_close_v2(db);
        return std::unexpected(std::move(e));
    }
    if (sqlite3_exec(db, kSchema, nullptr, nullptr, nullptr) != SQLITE_OK) {
        AuditError e = io(db, "schema");
        sqlite3_close_v2(db);
        return std::unexpected(std::move(e));
    }
    Store s;
    s.db_ = db;
    s.chain_id_ = std::move(chain_id);
    s.write_mutex_ = std::make_unique<std::mutex>();
    return s;
}

Store::Store(Store&& o) noexcept
    : db_(std::exchange(o.db_, nullptr)),
      chain_id_(std::move(o.chain_id_)),
      write_mutex_(std::move(o.write_mutex_)) {}

Store& Store::operator=(Store&& o) noexcept {
    if (this != &o) {
        if (db_ != nullptr) {
            sqlite3_close_v2(db_);  // _v2: never leaks the handle (review HIGH-2)
        }
        db_ = std::exchange(o.db_, nullptr);
        chain_id_ = std::move(o.chain_id_);
        write_mutex_ = std::move(o.write_mutex_);
    }
    return *this;
}

Store::~Store() {
    if (db_ != nullptr) {
        sqlite3_close_v2(db_);
    }
}

std::expected<AppendResult, AuditError> Store::append(const Event& ev) {
    const std::lock_guard<std::mutex> lock(*write_mutex_);  // H-21 single-writer
    Txn txn(db_);
    if (!txn.began()) {
        return std::unexpected(io(db_, "begin"));
    }
    auto r = append_locked(db_, chain_id_, ev);
    if (!r) {
        return r;
    }
    if (!txn.commit()) {
        return std::unexpected(io(db_, "commit"));
    }
    return r;
}

std::expected<AppendResult, AuditError> Store::create_entity(const CreateEntityRequest& req) {
    if (req.entity_id.empty() || req.created_by.empty()) {
        return std::unexpected(
            AuditError(AuditErrc::InvalidArgument, "entity_id and created_by required"));
    }

    // Creating directly at APPROVED/CERTIFIED requires a human actor (C-1.1, R-2.3).
    // AI_PROPOSED and REVIEWED are agent-accessible by design (spec §3.3 guards 1–2).
    if (static_cast<int>(req.initial_class) >= static_cast<int>(AuthorityClass::Approved) &&
        req.created_by_kind != ActorKind::Human) {
        return std::unexpected(
            AuditError(AuditErrc::AuthorityViolation,
                       "non-human cannot create an entity at APPROVED/CERTIFIED"));
    }

    const std::lock_guard<std::mutex> lock(*write_mutex_);  // H-21 single-writer
    Txn txn(db_);
    if (!txn.began()) {
        return std::unexpected(io(db_, "begin"));
    }

    // Reject duplicate entity.
    {
        Stmt dup(db_, "SELECT 1 FROM entity WHERE entity_id=?");
        if (!dup.ok()) {
            return std::unexpected(io(db_, "prepare duplicate check"));
        }
        dup.bind_text(1, req.entity_id);
        if (dup.step() == SQLITE_ROW) {
            return std::unexpected(AuditError(AuditErrc::InvalidArgument, "entity already exists"));
        }
    }

    // Payload separators + escaping match Python json.dumps for cross-language parity.
    const std::string payload =
        std::string("{\"entity_id\": \"") + json_escape(req.entity_id) +
        "\", \"to_class\": " + std::to_string(static_cast<int>(req.initial_class)) + "}";
    auto ap = append_locked(db_, chain_id_, {"ENTITY_CREATED", payload, req.timestamp});
    if (!ap) {
        return ap;
    }

    // entity row
    {
        Stmt ins(db_,
                 "INSERT INTO entity(entity_id,authority_class,source_agent,created_by,"
                 "created_at,approved_by,approved_at,confidence,verification_state,head_seq)"
                 " VALUES(?,?,?,?,?,NULL,NULL,?,?,?)");
        if (!ins.ok()) {
            return std::unexpected(io(db_, "prepare insert entity"));
        }
        ins.bind_text(1, req.entity_id);
        ins.bind_int(2, static_cast<int>(req.initial_class));
        if (req.source_agent) {
            ins.bind_text(3, *req.source_agent);
        } else {
            ins.bind_null(3);
        }
        ins.bind_text(4, req.created_by);
        ins.bind_text(5, req.timestamp);
        if (req.confidence) {
            ins.bind_double(6, *req.confidence);
        } else {
            ins.bind_null(6);
        }
        ins.bind_static(7, "UNVERIFIED");
        ins.bind_int64(8, ap->seq);
        if (ins.step() != SQLITE_DONE) {
            return std::unexpected(io(db_, "insert entity"));
        }
    }

    // creation promotion (from_class NULL)
    auto pr = insert_promotion(db_, ap->seq, req.entity_id, std::nullopt,
                               static_cast<int>(req.initial_class), req.created_by,
                               req.created_by_kind, "creation");
    if (!pr) {
        return std::unexpected(pr.error());
    }

    if (!txn.commit()) {
        return std::unexpected(io(db_, "commit"));
    }
    return ap;
}

std::expected<AppendResult, AuditError> Store::promote(const PromotionRequest& req) {
    // Authority guard: APPROVED/CERTIFIED require a human actor (C-1.1, R-2.3).
    if (static_cast<int>(req.to_class) >= static_cast<int>(AuthorityClass::Approved) &&
        req.actor_kind != ActorKind::Human) {
        return std::unexpected(
            AuditError(AuditErrc::AuthorityViolation,
                       "promotion to APPROVED/CERTIFIED requires a human-attributed action"));
    }

    const std::lock_guard<std::mutex> lock(*write_mutex_);  // H-21 single-writer
    Txn txn(db_);
    if (!txn.began()) {
        return std::unexpected(io(db_, "begin"));
    }

    // Load current projected class + head_seq.
    int from_class = -1;
    std::int64_t head = 0;
    {
        Stmt cur(db_, "SELECT authority_class, head_seq FROM entity WHERE entity_id=?");
        if (!cur.ok()) {
            return std::unexpected(io(db_, "prepare entity lookup"));
        }
        cur.bind_text(1, req.entity_id);
        if (cur.step() != SQLITE_ROW) {
            return std::unexpected(AuditError(AuditErrc::NotFound, "entity not found"));
        }
        from_class = sqlite3_column_int(cur.get(), 0);
        head = sqlite3_column_int64(cur.get(), 1);
    }

    // Optimistic concurrency (spec §3.3 guard 3).
    if (req.expected_head_seq != head) {
        return std::unexpected(AuditError(AuditErrc::ConcurrencyConflict, "stale head_seq"));
    }
    // Monotonic forward transition only (this phase): to_class must exceed current.
    if (static_cast<int>(req.to_class) <= from_class) {
        return std::unexpected(
            AuditError(AuditErrc::InvalidArgument, "to_class must be greater than current class"));
    }

    const std::string payload =
        std::string("{\"entity_id\": \"") + json_escape(req.entity_id) +
        "\", \"from_class\": " + std::to_string(from_class) +
        ", \"to_class\": " + std::to_string(static_cast<int>(req.to_class)) + "}";
    auto ap = append_locked(db_, chain_id_, {"PROMOTION", payload, req.timestamp});
    if (!ap) {
        return ap;
    }

    auto pr =
        insert_promotion(db_, ap->seq, req.entity_id, from_class, static_cast<int>(req.to_class),
                         req.actor, req.actor_kind, req.reason);
    if (!pr) {
        return std::unexpected(pr.error());
    }

    // Update entity projection (append-only chain remains the source of truth).
    {
        const bool approving =
            static_cast<int>(req.to_class) >= static_cast<int>(AuthorityClass::Approved);
        Stmt up(db_,
                "UPDATE entity SET authority_class=?, head_seq=?,"
                " approved_by=COALESCE(?,approved_by), approved_at=COALESCE(?,approved_at)"
                " WHERE entity_id=?");
        if (!up.ok()) {
            return std::unexpected(io(db_, "prepare entity projection update"));
        }
        up.bind_int(1, static_cast<int>(req.to_class));
        up.bind_int64(2, ap->seq);
        if (approving) {
            up.bind_text(3, req.actor);
            up.bind_text(4, req.timestamp);
        } else {
            up.bind_null(3);
            up.bind_null(4);
        }
        up.bind_text(5, req.entity_id);
        if (up.step() != SQLITE_DONE) {
            return std::unexpected(io(db_, "update entity projection"));
        }
    }

    if (!txn.commit()) {
        return std::unexpected(io(db_, "commit"));
    }
    return ap;
}

std::expected<void, AuditError> Store::verify_chain() const {
    Stmt st(db_,
            "SELECT seq,ts,chain_id,event_type,payload_json,prev_hash,hash FROM event"
            " ORDER BY seq ASC");
    if (!st.ok()) {
        return std::unexpected(io(db_, "prepare verify"));
    }

    std::string expected_prev = kGenesis;
    std::int64_t expected_seq = 1;
    while (st.step() == SQLITE_ROW) {
        const std::int64_t seq = sqlite3_column_int64(st.get(), 0);
        const auto ts = col_text(st.get(), 1);
        const auto chain = col_text(st.get(), 2);
        const auto etype = col_text(st.get(), 3);
        const auto payload = col_text(st.get(), 4);
        const auto prev = col_text(st.get(), 5);
        const auto hash = col_text(st.get(), 6);

        // A NULL in a NOT NULL column means the file is corrupted or was written outside
        // this library — distinct failure from a hash mismatch (review CRIT-1/MED-1).
        if (!ts || !chain || !etype || !payload || !prev || !hash) {
            return std::unexpected(
                AuditError(AuditErrc::ChainBroken,
                           "NULL column at seq " + std::to_string(seq) + " (corrupted database)"));
        }
        if (seq != expected_seq) {
            return std::unexpected(
                AuditError(AuditErrc::ChainBroken, "seq gap at " + std::to_string(seq)));
        }
        if (expected_prev != *prev) {
            return std::unexpected(AuditError(AuditErrc::ChainBroken,
                                              "prev_hash mismatch at seq " + std::to_string(seq)));
        }
        const std::string record = canonical_record(seq, *ts, *chain, *etype, *payload, *prev);
        if (sha256_hex(record) != *hash) {
            return std::unexpected(
                AuditError(AuditErrc::ChainBroken, "hash mismatch at seq " + std::to_string(seq)));
        }
        expected_prev = *hash;
        ++expected_seq;
    }
    return {};
}

std::expected<AuthorityClass, AuditError> Store::authority_of(const std::string& entity_id) const {
    Stmt st(db_, "SELECT authority_class FROM entity WHERE entity_id=?");
    if (!st.ok()) {
        return std::unexpected(io(db_, "prepare authority_of"));
    }
    st.bind_text(1, entity_id);
    if (st.step() != SQLITE_ROW) {
        return std::unexpected(AuditError(AuditErrc::NotFound, "entity not found"));
    }
    return static_cast<AuthorityClass>(sqlite3_column_int(st.get(), 0));
}

std::expected<std::vector<Entity>, AuditError> Store::certified_snapshot() const {
    Stmt st(db_,
            "SELECT entity_id,authority_class,source_agent,created_by,created_at,approved_by,"
            "approved_at,confidence,verification_state,head_seq FROM certified_snapshot"
            " ORDER BY entity_id");
    if (!st.ok()) {
        return std::unexpected(io(db_, "prepare snapshot"));
    }

    std::vector<Entity> out;
    while (st.step() == SQLITE_ROW) {
        const auto entity_id = col_text(st.get(), 0);
        const auto created_by = col_text(st.get(), 3);
        const auto created_at = col_text(st.get(), 4);
        const auto verification = col_text(st.get(), 8);
        if (!entity_id || !created_by || !created_at || !verification) {
            return std::unexpected(
                AuditError(AuditErrc::Io, "NULL in NOT NULL entity column (corrupted database)"));
        }
        Entity e;
        e.entity_id = *entity_id;
        e.authority_class = static_cast<AuthorityClass>(sqlite3_column_int(st.get(), 1));
        e.source_agent = col_text(st.get(), 2);
        e.created_by = *created_by;
        e.created_at = *created_at;
        e.approved_by = col_text(st.get(), 5);
        e.approved_at = col_text(st.get(), 6);
        if (sqlite3_column_type(st.get(), 7) != SQLITE_NULL) {
            e.confidence = sqlite3_column_double(st.get(), 7);
        }
        e.verification_state = *verification;
        e.head_seq = sqlite3_column_int64(st.get(), 9);
        out.push_back(std::move(e));
    }
    return out;
}

std::expected<std::int64_t, AuditError> Store::event_count() const {
    Stmt st(db_, "SELECT COUNT(*) FROM event");
    if (!st.ok() || st.step() != SQLITE_ROW) {
        return std::unexpected(io(db_, "event_count"));
    }
    return sqlite3_column_int64(st.get(), 0);
}

}  // namespace ingeneer::audit
