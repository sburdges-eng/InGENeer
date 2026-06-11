#include "ingeneer/audit/store.hpp"

#include <sqlite3.h>

#include <utility>

#include "ingeneer/audit/canonical.hpp"
#include "ingeneer/audit/sha256.hpp"

namespace ingeneer::audit {
namespace {

constexpr const char* kGenesis = "0000000000000000000000000000000000000000000000000000000000000000";

const char* kSchema = R"sql(
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
    if (db) {
        msg += ": ";
        msg += sqlite3_errmsg(db);
    }
    return AuditError(AuditErrc::Io, std::move(msg));
}

// Append one event inside an already-open transaction. Returns the new head.
std::expected<AppendResult, AuditError> append_locked(sqlite3* db, const Event& ev) {
    // Determine seq and prev_hash from the current head.
    std::int64_t seq = 1;
    std::string prev = kGenesis;
    {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT seq, hash FROM event ORDER BY seq DESC LIMIT 1", -1, &st,
                               nullptr) != SQLITE_OK)
            return std::unexpected(io(db, "prepare head"));
        if (sqlite3_step(st) == SQLITE_ROW) {
            seq = sqlite3_column_int64(st, 0) + 1;
            prev = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
        }
        sqlite3_finalize(st);
    }

    const std::string record =
        canonical_record(seq, ev.timestamp, ev.chain_id, ev.event_type, ev.payload_json, prev);
    const std::string hash = sha256_hex(record);

    sqlite3_stmt* ins = nullptr;
    if (sqlite3_prepare_v2(
            db,
            "INSERT INTO event(seq,ts,chain_id,event_type,payload_json,prev_hash,hash)"
            " VALUES(?,?,?,?,?,?,?)",
            -1, &ins, nullptr) != SQLITE_OK)
        return std::unexpected(io(db, "prepare insert event"));

    sqlite3_bind_int64(ins, 1, seq);
    sqlite3_bind_text(ins, 2, ev.timestamp.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 3, ev.chain_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 4, ev.event_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 5, ev.payload_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 6, prev.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 7, hash.c_str(), -1, SQLITE_TRANSIENT);
    const int rc = sqlite3_step(ins);
    sqlite3_finalize(ins);
    if (rc != SQLITE_DONE) return std::unexpected(io(db, "insert event"));

    return AppendResult{seq, hash};
}

bool exec(sqlite3* db, const char* sql) {
    return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

}  // namespace

std::expected<Store, AuditError> Store::open(const std::string& path) {
    sqlite3* db = nullptr;
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
        AuditError e = io(db, "open");
        sqlite3_close(db);
        return std::unexpected(std::move(e));
    }
    if (sqlite3_exec(db, kSchema, nullptr, nullptr, nullptr) != SQLITE_OK) {
        AuditError e = io(db, "schema");
        sqlite3_close(db);
        return std::unexpected(std::move(e));
    }
    Store s;
    s.db_ = db;
    return s;
}

Store::Store(Store&& o) noexcept : db_(std::exchange(o.db_, nullptr)) {}
Store& Store::operator=(Store&& o) noexcept {
    if (this != &o) {
        if (db_) sqlite3_close(db_);
        db_ = std::exchange(o.db_, nullptr);
    }
    return *this;
}
Store::~Store() {
    if (db_) sqlite3_close(db_);
}

std::expected<AppendResult, AuditError> Store::append(const Event& ev) {
    if (!exec(db_, "BEGIN IMMEDIATE")) return std::unexpected(io(db_, "begin"));
    auto r = append_locked(db_, ev);
    if (!r) {
        exec(db_, "ROLLBACK");
        return r;
    }
    if (!exec(db_, "COMMIT")) return std::unexpected(io(db_, "commit"));
    return r;
}

std::expected<AppendResult, AuditError> Store::create_entity(const CreateEntityRequest& req) {
    if (req.entity_id.empty() || req.created_by.empty())
        return std::unexpected(
            AuditError(AuditErrc::InvalidArgument, "entity_id and created_by required"));

    // Creating directly at APPROVED/CERTIFIED requires a human actor (C-1.1, R-2.3).
    if (static_cast<int>(req.initial_class) >= static_cast<int>(AuthorityClass::Approved) &&
        req.created_by_kind != ActorKind::Human)
        return std::unexpected(
            AuditError(AuditErrc::AuthorityViolation,
                       "non-human cannot create an entity at APPROVED/CERTIFIED"));

    if (!exec(db_, "BEGIN IMMEDIATE")) return std::unexpected(io(db_, "begin"));

    // Reject duplicate entity.
    {
        sqlite3_stmt* st = nullptr;
        sqlite3_prepare_v2(db_, "SELECT 1 FROM entity WHERE entity_id=?", -1, &st, nullptr);
        sqlite3_bind_text(st, 1, req.entity_id.c_str(), -1, SQLITE_TRANSIENT);
        const bool exists = sqlite3_step(st) == SQLITE_ROW;
        sqlite3_finalize(st);
        if (exists) {
            exec(db_, "ROLLBACK");
            return std::unexpected(AuditError(AuditErrc::InvalidArgument, "entity already exists"));
        }
    }

    // Payload separators match Python json.dumps(sort_keys=True) for cross-language parity.
    const std::string payload =
        std::string("{\"entity_id\": \"") + json_escape(req.entity_id) +
        "\", \"to_class\": " + std::to_string(static_cast<int>(req.initial_class)) + "}";
    Event ev{"ENTITY_CREATED", payload, req.entity_id, req.timestamp};
    auto ap = append_locked(db_, ev);
    if (!ap) {
        exec(db_, "ROLLBACK");
        return ap;
    }

    // entity row
    sqlite3_stmt* ins = nullptr;
    sqlite3_prepare_v2(
        db_,
        "INSERT INTO entity(entity_id,authority_class,source_agent,created_by,created_at,"
        "approved_by,approved_at,confidence,verification_state,head_seq)"
        " VALUES(?,?,?,?,?,NULL,NULL,?,?,?)",
        -1, &ins, nullptr);
    sqlite3_bind_text(ins, 1, req.entity_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(ins, 2, static_cast<int>(req.initial_class));
    if (req.source_agent)
        sqlite3_bind_text(ins, 3, req.source_agent->c_str(), -1, SQLITE_TRANSIENT);
    else
        sqlite3_bind_null(ins, 3);
    sqlite3_bind_text(ins, 4, req.created_by.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 5, req.timestamp.c_str(), -1, SQLITE_TRANSIENT);
    if (req.confidence)
        sqlite3_bind_double(ins, 6, *req.confidence);
    else
        sqlite3_bind_null(ins, 6);
    sqlite3_bind_text(ins, 7, "UNVERIFIED", -1, SQLITE_STATIC);
    sqlite3_bind_int64(ins, 8, ap->seq);
    const int rc = sqlite3_step(ins);
    sqlite3_finalize(ins);
    if (rc != SQLITE_DONE) {
        exec(db_, "ROLLBACK");
        return std::unexpected(io(db_, "insert entity"));
    }

    // creation promotion (from_class NULL)
    sqlite3_stmt* pr = nullptr;
    sqlite3_prepare_v2(db_,
                       "INSERT INTO promotion(seq,entity_id,from_class,to_class,actor,"
                       "actor_kind,reason) VALUES(?,?,NULL,?,?,?,?)",
                       -1, &pr, nullptr);
    sqlite3_bind_int64(pr, 1, ap->seq);
    sqlite3_bind_text(pr, 2, req.entity_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(pr, 3, static_cast<int>(req.initial_class));
    sqlite3_bind_text(pr, 4, req.created_by.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(pr, 5, req.created_by_kind == ActorKind::Human ? "human" : "agent", -1,
                      SQLITE_STATIC);
    sqlite3_bind_text(pr, 6, "creation", -1, SQLITE_STATIC);
    const int rc2 = sqlite3_step(pr);
    sqlite3_finalize(pr);
    if (rc2 != SQLITE_DONE) {
        exec(db_, "ROLLBACK");
        return std::unexpected(io(db_, "insert creation promotion"));
    }

    if (!exec(db_, "COMMIT")) return std::unexpected(io(db_, "commit"));
    return ap;
}

std::expected<AppendResult, AuditError> Store::promote(const PromotionRequest& req) {
    // Authority guard: APPROVED/CERTIFIED require a human actor (C-1.1, R-2.3).
    if (static_cast<int>(req.to_class) >= static_cast<int>(AuthorityClass::Approved) &&
        req.actor_kind != ActorKind::Human)
        return std::unexpected(
            AuditError(AuditErrc::AuthorityViolation,
                       "promotion to APPROVED/CERTIFIED requires a human-attributed action"));

    if (!exec(db_, "BEGIN IMMEDIATE")) return std::unexpected(io(db_, "begin"));

    // Load current projected class + head_seq.
    int from_class = -1;
    std::int64_t head = 0;
    {
        sqlite3_stmt* st = nullptr;
        sqlite3_prepare_v2(db_, "SELECT authority_class, head_seq FROM entity WHERE entity_id=?",
                           -1, &st, nullptr);
        sqlite3_bind_text(st, 1, req.entity_id.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st) != SQLITE_ROW) {
            sqlite3_finalize(st);
            exec(db_, "ROLLBACK");
            return std::unexpected(AuditError(AuditErrc::NotFound, "entity not found"));
        }
        from_class = sqlite3_column_int(st, 0);
        head = sqlite3_column_int64(st, 1);
        sqlite3_finalize(st);
    }

    // Optimistic concurrency (spec §3.3 guard 3).
    if (req.expected_head_seq != head) {
        exec(db_, "ROLLBACK");
        return std::unexpected(AuditError(AuditErrc::ConcurrencyConflict, "stale head_seq"));
    }
    // Monotonic forward transition only (this phase): to_class must exceed current.
    if (static_cast<int>(req.to_class) <= from_class) {
        exec(db_, "ROLLBACK");
        return std::unexpected(
            AuditError(AuditErrc::InvalidArgument, "to_class must be greater than current class"));
    }

    const std::string payload =
        std::string("{\"entity_id\": \"") + json_escape(req.entity_id) +
        "\", \"from_class\": " + std::to_string(from_class) +
        ", \"to_class\": " + std::to_string(static_cast<int>(req.to_class)) + "}";
    Event ev{"PROMOTION", payload, req.entity_id, req.timestamp};
    auto ap = append_locked(db_, ev);
    if (!ap) {
        exec(db_, "ROLLBACK");
        return ap;
    }

    sqlite3_stmt* pr = nullptr;
    sqlite3_prepare_v2(db_,
                       "INSERT INTO promotion(seq,entity_id,from_class,to_class,actor,"
                       "actor_kind,reason) VALUES(?,?,?,?,?,?,?)",
                       -1, &pr, nullptr);
    sqlite3_bind_int64(pr, 1, ap->seq);
    sqlite3_bind_text(pr, 2, req.entity_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(pr, 3, from_class);
    sqlite3_bind_int(pr, 4, static_cast<int>(req.to_class));
    sqlite3_bind_text(pr, 5, req.actor.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(pr, 6, req.actor_kind == ActorKind::Human ? "human" : "agent", -1,
                      SQLITE_STATIC);
    sqlite3_bind_text(pr, 7, req.reason.c_str(), -1, SQLITE_TRANSIENT);
    const int rc = sqlite3_step(pr);
    sqlite3_finalize(pr);
    if (rc != SQLITE_DONE) {
        exec(db_, "ROLLBACK");
        return std::unexpected(io(db_, "insert promotion"));
    }

    // Update entity projection (append-only chain remains the source of truth).
    sqlite3_stmt* up = nullptr;
    const bool approving =
        static_cast<int>(req.to_class) >= static_cast<int>(AuthorityClass::Approved);
    sqlite3_prepare_v2(
        db_,
        "UPDATE entity SET authority_class=?, head_seq=?, approved_by=COALESCE(?,approved_by),"
        " approved_at=COALESCE(?,approved_at) WHERE entity_id=?",
        -1, &up, nullptr);
    sqlite3_bind_int(up, 1, static_cast<int>(req.to_class));
    sqlite3_bind_int64(up, 2, ap->seq);
    if (approving) {
        sqlite3_bind_text(up, 3, req.actor.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(up, 4, req.timestamp.c_str(), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(up, 3);
        sqlite3_bind_null(up, 4);
    }
    sqlite3_bind_text(up, 5, req.entity_id.c_str(), -1, SQLITE_TRANSIENT);
    const int rc3 = sqlite3_step(up);
    sqlite3_finalize(up);
    if (rc3 != SQLITE_DONE) {
        exec(db_, "ROLLBACK");
        return std::unexpected(io(db_, "update entity projection"));
    }

    if (!exec(db_, "COMMIT")) return std::unexpected(io(db_, "commit"));
    return ap;
}

std::expected<void, AuditError> Store::verify_chain() const {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(
            db_,
            "SELECT seq,ts,chain_id,event_type,payload_json,prev_hash,hash FROM event"
            " ORDER BY seq ASC",
            -1, &st, nullptr) != SQLITE_OK)
        return std::unexpected(io(db_, "prepare verify"));

    std::string expected_prev = kGenesis;
    std::int64_t expected_seq = 1;
    while (sqlite3_step(st) == SQLITE_ROW) {
        const std::int64_t seq = sqlite3_column_int64(st, 0);
        const char* ts = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
        const char* chain = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
        const char* etype = reinterpret_cast<const char*>(sqlite3_column_text(st, 3));
        const char* payload = reinterpret_cast<const char*>(sqlite3_column_text(st, 4));
        const char* prev = reinterpret_cast<const char*>(sqlite3_column_text(st, 5));
        const char* hash = reinterpret_cast<const char*>(sqlite3_column_text(st, 6));

        if (seq != expected_seq) {
            sqlite3_finalize(st);
            return std::unexpected(
                AuditError(AuditErrc::ChainBroken, "seq gap at " + std::to_string(seq)));
        }
        if (expected_prev != prev) {
            sqlite3_finalize(st);
            return std::unexpected(AuditError(AuditErrc::ChainBroken,
                                              "prev_hash mismatch at seq " + std::to_string(seq)));
        }
        const std::string record = canonical_record(seq, ts, chain, etype, payload, prev);
        const std::string computed = sha256_hex(record);
        if (computed != hash) {
            sqlite3_finalize(st);
            return std::unexpected(
                AuditError(AuditErrc::ChainBroken, "hash mismatch at seq " + std::to_string(seq)));
        }
        expected_prev = hash;
        ++expected_seq;
    }
    sqlite3_finalize(st);
    return {};
}

std::expected<AuthorityClass, AuditError> Store::authority_of(const std::string& entity_id) const {
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "SELECT authority_class FROM entity WHERE entity_id=?", -1, &st,
                       nullptr);
    sqlite3_bind_text(st, 1, entity_id.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) != SQLITE_ROW) {
        sqlite3_finalize(st);
        return std::unexpected(AuditError(AuditErrc::NotFound, "entity not found"));
    }
    const int cls = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return static_cast<AuthorityClass>(cls);
}

std::expected<std::vector<Entity>, AuditError> Store::certified_snapshot() const {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_,
                           "SELECT entity_id,authority_class,source_agent,created_by,"
                           "created_at,approved_by,approved_at,confidence,verification_state,"
                           "head_seq FROM certified_snapshot ORDER BY entity_id",
                           -1, &st, nullptr) != SQLITE_OK)
        return std::unexpected(io(db_, "prepare snapshot"));

    std::vector<Entity> out;
    while (sqlite3_step(st) == SQLITE_ROW) {
        Entity e;
        e.entity_id = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
        e.authority_class = static_cast<AuthorityClass>(sqlite3_column_int(st, 1));
        if (sqlite3_column_type(st, 2) != SQLITE_NULL)
            e.source_agent = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
        e.created_by = reinterpret_cast<const char*>(sqlite3_column_text(st, 3));
        e.created_at = reinterpret_cast<const char*>(sqlite3_column_text(st, 4));
        if (sqlite3_column_type(st, 5) != SQLITE_NULL)
            e.approved_by = reinterpret_cast<const char*>(sqlite3_column_text(st, 5));
        if (sqlite3_column_type(st, 6) != SQLITE_NULL)
            e.approved_at = reinterpret_cast<const char*>(sqlite3_column_text(st, 6));
        if (sqlite3_column_type(st, 7) != SQLITE_NULL) e.confidence = sqlite3_column_double(st, 7);
        e.verification_state = reinterpret_cast<const char*>(sqlite3_column_text(st, 8));
        e.head_seq = sqlite3_column_int64(st, 9);
        out.push_back(std::move(e));
    }
    sqlite3_finalize(st);
    return out;
}

std::expected<std::int64_t, AuditError> Store::event_count() const {
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM event", -1, &st, nullptr);
    sqlite3_step(st);
    const std::int64_t n = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    return n;
}

}  // namespace ingeneer::audit
