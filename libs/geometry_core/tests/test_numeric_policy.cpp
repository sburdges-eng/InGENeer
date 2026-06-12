// test_numeric_policy.cpp — locks the numeric-policy contract (Phase 5 exit gate).
//
// Ported from auracad tests/harness/numeric_policy_test.cpp (branch
// agentic/predicates-adaptive-3d-2026-04-24) per ADR-0024, rewritten on the
// repo's check.hpp harness. Verifies:
//   - The documented tolerance constants expose their published values, bit-exactly.
//   - The IEEE 754 / IEC 559 static_asserts in the header compile and hold at runtime.
//   - The tolerance ladder is well-ordered: DETERMINANT < EPSILON < LENGTH.
//   - kRobustPredicatesRequired == true, so modules can static_assert against it.
//   - NumericPolicy is final and non-instantiable.
//   - The header is includable in isolation — the only project include below is
//     "ingeneer/geom/numeric_policy.h"; everything else is stdlib.

#include "ingeneer/geom/numeric_policy.h"

#include <limits>
#include <type_traits>

#include "check.hpp"

using ingeneer::geom::numeric::ANGLE_EPSILON_DEG;
using ingeneer::geom::numeric::DETERMINANT_EPSILON;
using ingeneer::geom::numeric::EPSILON;
using ingeneer::geom::numeric::kRobustPredicatesRequired;
using ingeneer::geom::numeric::LENGTH_EPSILON;
using ingeneer::geom::numeric::NumericPolicy;

namespace {

void test_constant_values() {
    // Bit-equality: these are documented contract values, not approximations.
    // Verbatim from auracad numeric_policy.h per ADR-0024.
    static_assert(EPSILON == 1e-12);
    static_assert(LENGTH_EPSILON == 1e-9);
    static_assert(ANGLE_EPSILON_DEG == 1e-6);
    static_assert(DETERMINANT_EPSILON == 1e-15);
    CHECK(EPSILON == 1e-12);
    CHECK(LENGTH_EPSILON == 1e-9);
    CHECK(ANGLE_EPSILON_DEG == 1e-6);
    CHECK(DETERMINANT_EPSILON == 1e-15);
}

void test_runtime_introspection_surface() {
    // The class facade must return the same values as the inline constants.
    CHECK(NumericPolicy::epsilon() == EPSILON);
    CHECK(NumericPolicy::length_epsilon() == LENGTH_EPSILON);
    CHECK(NumericPolicy::angle_epsilon_deg() == ANGLE_EPSILON_DEG);
    CHECK(NumericPolicy::determinant_epsilon() == DETERMINANT_EPSILON);
    CHECK(NumericPolicy::robust_predicates_required() == kRobustPredicatesRequired);

    // These are constexpr; verify usability in a constexpr context.
    static_assert(NumericPolicy::epsilon() == 1e-12,
                  "NumericPolicy::epsilon() must be constexpr-evaluable");
    static_assert(NumericPolicy::robust_predicates_required(),
                  "NumericPolicy::robust_predicates_required() must be constexpr-evaluable");
}

void test_ieee754_guards() {
    // The header's static_asserts already enforce these at compile time. Restating them
    // here is living documentation: if a future edit weakens the header guards, this test
    // still catches the regression.
    static_assert(sizeof(double) == 8);
    static_assert(std::numeric_limits<double>::is_iec559);
    static_assert(std::numeric_limits<double>::digits == 53,
                  "binary64 mantissa must be 53 bits including implicit leading 1");
    static_assert(std::numeric_limits<double>::radix == 2, "binary64 must be radix 2");
    CHECK(sizeof(double) == 8);
    CHECK(std::numeric_limits<double>::is_iec559);
}

void test_tolerance_ladder_ordering() {
    // Design choice: length comparisons get a COARSER tolerance than the raw geometric-
    // equality epsilon (length math accrues rounding proportional to operand magnitude),
    // and the determinant filter threshold is a numerical threshold on a determinant
    // magnitude — correctly TIGHTER than EPSILON. Ladder: DETERMINANT < EPSILON < LENGTH.
    static_assert(DETERMINANT_EPSILON < EPSILON,
                  "DETERMINANT_EPSILON is a filter threshold and must be tighter than EPSILON");
    static_assert(EPSILON < LENGTH_EPSILON,
                  "EPSILON must be tighter than LENGTH_EPSILON; see numeric_policy.h");
    CHECK(DETERMINANT_EPSILON < EPSILON);
    CHECK(EPSILON < LENGTH_EPSILON);

    // All tolerances are strictly positive and finite.
    CHECK(EPSILON > 0.0);
    CHECK(LENGTH_EPSILON > 0.0);
    CHECK(ANGLE_EPSILON_DEG > 0.0);
    CHECK(DETERMINANT_EPSILON > 0.0);
    CHECK(EPSILON < std::numeric_limits<double>::infinity());
    CHECK(LENGTH_EPSILON < std::numeric_limits<double>::infinity());
    CHECK(ANGLE_EPSILON_DEG < std::numeric_limits<double>::infinity());
    CHECK(DETERMINANT_EPSILON < std::numeric_limits<double>::infinity());
}

void test_robust_predicates_flag() {
    static_assert(kRobustPredicatesRequired,
                  "kRobustPredicatesRequired must remain true; see numeric_policy.h");
    CHECK(kRobustPredicatesRequired == true);
}

void test_class_is_non_instantiable() {
    // NumericPolicy is a pure facade. All special members are deleted; verify at compile
    // time that nothing can construct, copy, or move it, and that it is final.
    static_assert(!std::is_default_constructible_v<NumericPolicy>);
    static_assert(!std::is_copy_constructible_v<NumericPolicy>);
    static_assert(!std::is_move_constructible_v<NumericPolicy>);
    static_assert(!std::is_copy_assignable_v<NumericPolicy>);
    static_assert(!std::is_move_assignable_v<NumericPolicy>);
    static_assert(std::is_final_v<NumericPolicy>);
}

void run() {
    test_constant_values();
    test_runtime_introspection_surface();
    test_ieee754_guards();
    test_tolerance_ladder_ordering();
    test_robust_predicates_flag();
    test_class_is_non_instantiable();
}

}  // namespace

TEST_MAIN_RUN()
