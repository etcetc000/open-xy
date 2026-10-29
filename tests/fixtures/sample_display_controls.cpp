#include "../../sdk/platform/opxy-1.2.1/sample_display_controls.hpp"
#include <cstdlib>
void require(bool value) { if (!value) std::abort(); }
int main() {
    using opxy::platform::sample_display_page_valid;
    require(!sample_display_page_valid(0));
    require(!sample_display_page_valid(81));
    require(sample_display_page_valid(82));
    require(sample_display_page_valid(127));
    require(sample_display_page_valid(128));
    require(sample_display_page_valid(251));
    require(sample_display_page_valid(255));
    require(!sample_display_page_valid(256));
    require(!sample_display_page_valid(0xffffffffu));
    opxy::platform::SampleDisplayKey key = {};
    require(!key.consume(23, false, true)); // Never own unmatched release.
    require(!key.consume(23, true, false)); // Stock press stays stock.
    require(!key.consume(23, false, true));
    require(key.consume(23, true, true));
    require(key.owned);
    require(key.consume(23, true, false)); // Repeat after leaving page.
    require(!key.consume(55, false, true)); // Shift release remains stock.
    require(key.owned);
    require(key.consume(23, false, false)); // Navigation cannot leak release.
    require(!key.owned);
    require(!key.consume(23, false, true));
    require(key.consume(23, true, true)); // Next intentional gesture rearms.
    require(key.consume(23, false, true));
}
