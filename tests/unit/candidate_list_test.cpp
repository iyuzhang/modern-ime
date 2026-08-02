#include <catch2/catch_test_macros.hpp>

#include <fcitx/candidatelist.h>

namespace {
class TestCandidate final : public fcitx::CandidateWord {
public:
    explicit TestCandidate(std::string text) : CandidateWord(fcitx::Text(std::move(text))) {}
    void select(fcitx::InputContext *) const override {}
};
}

TEST_CASE("Fcitx candidate list exposes the current page through candidate()") {
    fcitx::CommonCandidateList candidates;
    candidates.setPageSize(2);
    candidates.append<TestCandidate>("first");
    candidates.append<TestCandidate>("second");
    candidates.append<TestCandidate>("third");
    candidates.append<TestCandidate>("fourth");

    REQUIRE(candidates.currentPage() == 0);
    REQUIRE(candidates.candidate(0).text().toString() == "first");
    candidates.next();
    REQUIRE(candidates.currentPage() == 1);
    REQUIRE(candidates.candidate(0).text().toString() == "third");
}

TEST_CASE("Fcitx cursor movement stays on the current page when requested") {
    fcitx::CommonCandidateList candidates;
    candidates.setPageSize(2);
    candidates.setCursorKeepInSamePage(true);
    candidates.append<TestCandidate>("first");
    candidates.append<TestCandidate>("second");
    candidates.append<TestCandidate>("third");
    candidates.append<TestCandidate>("fourth");

    candidates.next();
    REQUIRE(candidates.currentPage() == 1);
    candidates.nextCandidate();
    candidates.nextCandidate();
    REQUIRE(candidates.currentPage() == 1);
    REQUIRE(candidates.cursorIndex() == 1);
    candidates.prevCandidate();
    REQUIRE(candidates.currentPage() == 1);
    REQUIRE(candidates.cursorIndex() == 0);
}
