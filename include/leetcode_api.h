#pragma once
#include <functional>
#include <string>
#include <vector>

namespace leetcli {
    struct ProblemSummary {
        std::string id;
        std::string title;
        std::string slug;
        std::string difficulty;
    };

    struct ProblemPage {
        std::vector<ProblemSummary> problems;
        int total = 0;
        std::string error;
    };

    // Fetches one page of 100 problems. Pass keyword="" for unfiltered paged browsing.
    ProblemPage fetch_problem_page(int skip, const std::string& keyword = "");

    // Fetches today's daily challenge as a ProblemSummary. An empty `slug` in
    // the result means it couldn't be fetched.
    ProblemSummary fetch_daily_problem();

    struct ProblemDescription {
        std::string title;
        std::string content_html;  // raw HTML, render with html_to_ftxui()
        std::string error;
    };

    // Fetches a problem's title/description HTML for preview only — no
    // files written to disk. Used by the TUI's Browse All tab to show the
    // full description for whichever problem is explicitly selected
    // (distinct from merely hovered while arrow-key navigating the list),
    // without persisting anything until the user actually fetches it into
    // My Problems.
    ProblemDescription fetch_problem_description(const std::string& slug);

    struct TopicsHintsResult {
        std::vector<std::string> topics;
        std::vector<std::string> hints;
        std::string error;
    };

    // Fetches a problem's topic tags and hints in a single request. Used to
    // lazily populate topics.txt/hints.txt for problems fetched before that
    // cache existed.
    TopicsHintsResult fetch_topics_and_hints(const std::string& slug);

    struct StartSolutionResult {
        std::string solution_path;  // empty on failure
        std::string error;
    };

    // Fetches starter code for a single language (langSlug, e.g. "cpp",
    // "python3", "rust", ...) and writes solution.<ext> into an
    // already-fetched problem's folder. `ext` (e.g. ".cpp") is taken as-is
    // from the caller rather than derived here, since the TUI's language
    // list (tui.cpp's kLangOptions) is the single source of truth for the
    // lang->extension mapping. Used by the TUI's "Start solution" prompt for
    // problems that don't have a solution file yet — unlike fetch_problem(),
    // this doesn't re-write README/description.html/topics/hints/testcases.
    StartSolutionResult write_starter_solution(const std::string& slug,
                                                const std::string& folder,
                                                const std::string& lang,
                                                const std::string& ext);

    struct TestcaseResult {
        std::string input;
        std::string expected;
        std::string actual;
        bool passed = false;
    };

    struct RunResult {
        std::string error;          // transport-level error (HTTP/timeout); empty on success
        std::string status_msg;     // "Accepted", "Wrong Answer", "Runtime Error", "Compile Error", ...
        bool run_success = false;
        bool correct_answer = false;
        std::string compile_error;
        std::string runtime_error;  // combined runtime_error/full_runtime_error/std_output_list text
        std::string status_runtime;
        std::string status_memory;
        std::vector<TestcaseResult> testcases;  // one entry per local example testcase
    };

    // Runs the given code against every example testcase in
    // <folder>/testcases.txt in a single request (LeetCode's "Run" — not a
    // real submission, doesn't touch submission history). Used by the TUI's
    // Solution-panel "Run" tab.
    RunResult run_against_testcases(const std::string& slug, const std::string& folder,
                                     const std::string& lang, const std::string& code);

    struct DistributionPoint {
        // Runtime buckets are in ms (matches status_runtime's unit, e.g.
        // "4 ms"). Memory buckets are in KB — NOT MB, unlike status_memory
        // (e.g. "14.9 MB"); convert before comparing the two.
        std::string bucket;
        double percentage = 0.0;
    };

    struct SubmitResult {
        std::string error;          // transport-level error; empty on success
        std::string status_msg;     // "Accepted", "Wrong Answer", "Time Limit Exceeded", ...
        bool accepted = false;
        std::string status_runtime;
        std::string status_memory;
        double runtime_percentile = -1;  // -1 = not present in the response
        double memory_percentile = -1;
        std::vector<DistributionPoint> runtime_distribution;
        std::vector<DistributionPoint> memory_distribution;
        std::string compile_error;
        int total_correct = 0;
        int total_testcases = 0;
        std::string input_formatted;
        std::string expected_output;
        std::string code_output;
    };

    // Submits the given code for real judging (adds an entry to LeetCode
    // submission history) and polls for the result. Used by the TUI's
    // Solution-panel "Submit" tab.
    SubmitResult submit_and_poll(const std::string& slug, const std::string& folder,
                                 const std::string& lang, const std::string& code);

    // Progress snapshot for `sync_problems`. Passed to the on_progress
    // callback after each processed problem (and once at the end with
    // finished=true, or immediately with a non-empty `error` on failure).
    struct SyncProgress {
        int done = 0;               // problems processed so far
        int total = 0;              // total to process (0 until enumeration finishes)
        std::string current;        // slug currently being processed
        std::string last_result;    // short outcome of the just-finished item, e.g. "solved", "attempted (no code)"
        bool finished = false;      // true on the final callback
        std::string error;          // fatal error (auth/enumeration); empty on the happy path
    };

    // Downloads every solved + attempted problem on the logged-in account
    // locally: the problem folder (README/description/testcases/…) plus your
    // latest submission's code (most recent Accepted for solved problems, most
    // recent of any verdict for attempted ones), saved in the language you
    // solved it in. Writes a `.status` sidecar (ac/notac) per folder. Existing
    // folders are not re-downloaded, but a missing solution file is still
    // filled in. `limit` <= 0 means all; otherwise stop after `limit` problems.
    // `on_progress` (optional) is invoked from the calling thread after each
    // item — the CLI prints a line, the TUI posts a progress-bar redraw.
    // Requires a stored LeetCode session (see `leetcli login`).
    void sync_problems(int limit, const std::function<void(const SyncProgress&)>& on_progress = {});

    std::string get_daily_question_slug();
    std::string fetch_problem(const std::string& slug, const std::string& lang_override);
    std::string read_question_id_from_readme(const std::string& path);
    void solve_problem(const std::string& slug, const std::string &lang_override);
    void list_fetched_problems();
    void run_tests(const std::string& slug, const std::string &lang_override);
    void run_problem(const std::string& slug, const std::string& lang, const std::string& question_id,
    const std::string& code, const std::string& test_input, const std::string& session, const std::string& csrf);
    void submit_solution(const std::string& slug, const std::string &lang_override);
    void handle_config_command(const std::vector<std::string>& args);
    void analyze_runtime(const std::string& slug, const std::string &lang_override);
    void give_hint(const std::string& slug, const std::string &lang_override);
    void fetch_problem_topics(const std::string &slug);
    void fetch_problem_hints(const std::string &slug);
    // Deletes the local solution file(s) for a problem, leaving the rest of
    // its folder (README/description/testcases/…) intact.
    void reset_solution(const std::string &slug);
}
