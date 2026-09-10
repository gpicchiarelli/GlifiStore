#include "parse.hpp"
#include "test.hpp"

#include <limits>
#include <string>

GLYPHA_TEST("benchmark decimal size parser accepts complete representable values") {
    const auto zero = glyphastore::bench::parse_decimal_size("0");
    GLYPHA_REQUIRE(zero.has_value());
    GLYPHA_REQUIRE(*zero == 0U);

    const auto maximum_text = std::to_string(std::numeric_limits<std::size_t>::max());
    const auto maximum = glyphastore::bench::parse_decimal_size(maximum_text);
    GLYPHA_REQUIRE(maximum.has_value());
    GLYPHA_REQUIRE(*maximum == std::numeric_limits<std::size_t>::max());
}

GLYPHA_TEST("benchmark decimal size parser rejects partial signed and overflowing input") {
    GLYPHA_REQUIRE(!glyphastore::bench::parse_decimal_size("").has_value());
    GLYPHA_REQUIRE(!glyphastore::bench::parse_decimal_size("12x").has_value());
    GLYPHA_REQUIRE(!glyphastore::bench::parse_decimal_size("-1").has_value());
    GLYPHA_REQUIRE(!glyphastore::bench::parse_decimal_size("+1").has_value());
    GLYPHA_REQUIRE(!glyphastore::bench::parse_decimal_size(" 1").has_value());

    const auto overflow = std::to_string(std::numeric_limits<std::size_t>::max()) + "0";
    GLYPHA_REQUIRE(!glyphastore::bench::parse_decimal_size(overflow).has_value());
}
