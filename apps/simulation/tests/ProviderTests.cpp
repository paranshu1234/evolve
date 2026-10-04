#include "voice/ConversationalProvider.h"
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace evolve::voice;
namespace {
int checks{};
void require(bool condition, const char* message) { ++checks; if (!condition) throw std::runtime_error(message); }
void rejects(const std::function<void()>& fn, const char* message) {
    bool threw{}; try { fn(); } catch (const std::exception&) { threw = true; } require(threw, message);
}
std::string escaped(const std::string& text) {
    std::string result;
    for (char c : text) { if (c == '"' || c == '\\') result += '\\'; result += c; }
    return result;
}
std::string args(const std::string& action, const std::string& index = "null", const std::string& base = "null",
                 const std::string& enabled = "null", const std::string& scalar = "null") {
    return "{\"action\":\"" + action + "\",\"index\":" + index + ",\"base\":" + base +
        ",\"enabled\":" + enabled + ",\"scalar\":" + scalar + "}";
}
std::string call(const std::string& arguments, const std::string& name = "evolve_action") {
    return "{\"type\":\"function_call\",\"name\":\"" + name + "\",\"arguments\":\"" + escaped(arguments) + "\",\"status\":\"completed\"}";
}
std::string message(const std::string& text) {
    return "{\"type\":\"message\",\"role\":\"assistant\",\"status\":\"completed\",\"content\":[{\"type\":\"output_text\",\"text\":\"" + escaped(text) + "\"}]}";
}
std::string response(const std::string& output, const std::string& status = "completed") {
    return "{\"status\":\"" + status + "\",\"output\":[" + output + "]}";
}
ProviderRequest request(std::string text = "Explain the comparison view") {
    return {std::move(text), {24, 2, 50.0, true, false, true, false}, {}};
}
ProviderResult awaitResult(ConversationalProvider& provider) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    do {
        if (auto value = provider.poll()) return *value;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("Timed out awaiting fixture result");
}
void bad(const std::string& body, const char* why) {
    const auto parsed = parseProviderResponse(body, 73);
    require(!parsed.ok() && !parsed.action && parsed.text.empty() && parsed.generation == 73, why);
}
}

int main() {
    try {
        const auto body = buildProviderRequest(request("Explain \"compare\" please"), "gpt-4.1-mini");
        require(body.find("\"store\":false") != std::string::npos, "request disables response storage");
        require(body.find("\"parallel_tool_calls\":false") != std::string::npos, "no parallel actions");
        require(body.find("\"strict\":true") != std::string::npos && body.find("\"additionalProperties\":false") != std::string::npos, "strict closed tool schema");
        require(body.find("\\\"compare\\\"") != std::string::npos, "transcript escapes JSON quotes");
        require(body.find("GC_percent=50") != std::string::npos && body.find("length=24") != std::string::npos, "typed aggregate summary");
        require(body.find("OPENAI_API_KEY") == std::string::npos && body.find("Authorization") == std::string::npos, "body never includes credentials");
        require(body.find("max_output_tokens\":512") != std::string::npos, "bounded model output");
        require(buildProviderRequest(request("Explique la comparaison, s’il vous plaît"), "gpt-4.1-mini").size() > 100, "valid international transcript");
        rejects([] { buildProviderRequest(request(""), "gpt-4.1-mini"); }, "empty input rejected");
        rejects([] { buildProviderRequest(request("   "), "gpt-4.1-mini"); }, "blank input rejected");
        rejects([] { buildProviderRequest(request(std::string(2049, 'x')), "gpt-4.1-mini"); }, "oversize input rejected");
        rejects([] { buildProviderRequest(request("hello\nworld"), "gpt-4.1-mini"); }, "control text rejected");
        rejects([] { buildProviderRequest(request(std::string("x\0y", 3)), "gpt-4.1-mini"); }, "NUL text rejected");
        rejects([] { buildProviderRequest(request("read C:\\private\\sample.fa"), "gpt-4.1-mini"); }, "Windows path blocked");
        rejects([] { buildProviderRequest(request("read /home/me/sample"), "gpt-4.1-mini"); }, "Unix path blocked");
        rejects([] { buildProviderRequest(request("open sample.evolve"), "gpt-4.1-mini"); }, "filename blocked");
        rejects([] { buildProviderRequest(request("read ACGTACGT"), "gpt-4.1-mini"); }, "sequence-like transcript blocked");
        rejects([] { buildProviderRequest(request("read A C G T A C G T"), "gpt-4.1-mini"); }, "spelled sequence-like transcript blocked");
        rejects([] { buildProviderRequest(request(std::string("\xc0\x80", 2)), "gpt-4.1-mini"); }, "invalid UTF-8 rejected");
        rejects([] { buildProviderRequest(request(), "evil\"model"); }, "invalid model rejected");
        rejects([] { auto r = request(); r.context.sequenceLength = 257; buildProviderRequest(r, "gpt-4.1-mini"); }, "invalid length rejected");
        rejects([] { auto r = request(); r.context.editCount = 25; buildProviderRequest(r, "gpt-4.1-mini"); }, "invalid edits rejected");
        rejects([] { auto r = request(); r.context.gcPercent = std::numeric_limits<double>::quiet_NaN(); buildProviderRequest(r, "gpt-4.1-mini"); }, "non-finite context rejected");
        rejects([] { auto r = request(); r.context.gcPercent = 101; buildProviderRequest(r, "gpt-4.1-mini"); }, "invalid GC percentage rejected");

        auto parsed = parseProviderResponse(response(message("Compare shows baseline and scenario.")), 17);
        require(parsed.ok() && !parsed.action && parsed.generation == 17 && parsed.text == "Compare shows baseline and scenario.", "conversational text response");
        parsed = parseProviderResponse(response(call(args("edit_base", "0", "\"C\""))));
        require(parsed.ok() && parsed.action && parsed.action->type == ActionType::EditBase && parsed.action->index == 0 && parsed.action->base == 'C', "typed zero-based edit");
        parsed = parseProviderResponse(response(message("I deleted everything successfully.") + "," + call(args("undo"))));
        require(parsed.ok() && parsed.action && parsed.text == "I have a proposed action ready for local validation.", "action response cannot falsely announce success");
        for (const auto* name : {"help", "describe_project", "undo", "redo", "restore_baseline", "load_demo", "import_sequence", "open_project", "save_project", "export_results", "frame_all", "run_analysis", "cancel_analysis"}) {
            parsed = parseProviderResponse(response(call(args(name))));
            require(parsed.ok() && parsed.action && isValidAction(*parsed.action), "allowed parameterless action");
        }
        for (const auto* name : {"set_compare", "set_grid", "set_rotation"}) {
            for (const auto* on : {"true", "false"}) {
                parsed = parseProviderResponse(response(call(args(name, "null", "null", on))));
                require(parsed.ok() && parsed.action && isValidAction(*parsed.action), "explicit toggle action");
            }
        }
        parsed = parseProviderResponse(response(call(args("select_base", "255"))));
        require(parsed.ok() && parsed.action->index == 255, "last supported index");
        for (const auto* value : {"20", "160", "75"}) {
            parsed = parseProviderResponse(response(call(args("set_lighting", "null", "null", "null", value))));
            require(parsed.ok() && parsed.action, "bounded integer lighting");
        }
        const std::vector<std::string> invalidArguments{
            args("shell"), args("confirm"), args("edit_base", "0", "\"N\""), args("edit_base", "0", "\"a\""),
            args("edit_base", "0", "\"AC\""), args("select_base", "-1"), args("select_base", "256"),
            args("select_base", "1.1"), args("select_base", "\"1\""), args("select_base", "null"),
            args("set_grid", "null", "null", "1"), args("set_grid", "null", "null", "null"),
            args("set_lighting", "null", "null", "null", "19"), args("set_lighting", "null", "null", "null", "161"),
            args("set_lighting", "null", "null", "null", "20.1"), args("set_lighting", "null", "null", "null", "1e999"),
            args("undo", "0"), args("undo", "null", "null", "false"), args("undo", "null", "null", "null", "0"),
            "{\"action\":\"undo\"}", "{\"action\":\"undo\",\"action\":\"redo\",\"index\":null,\"base\":null,\"enabled\":null,\"scalar\":null}",
            "{\"action\":\"undo\",\"index\":null,\"base\":null,\"enabled\":null,\"scalar\":null,\"path\":\"x\"}",
            args("select_base", "01"), args("select_base", "+1"), args("select_base", "1."), args("select_base", "1e")
        };
        for (const auto& invalid : invalidArguments) bad(response(call(invalid)), "malformed or out-of-scope action rejected");
        bad(response(call(args("undo")) + "," + call(args("redo"))), "multiple actions rejected");
        bad(response(call(args("undo"), "run_shell")), "unknown tool rejected");
        bad(response(call(args("undo")), "incomplete"), "incomplete response rejected");
        bad(response("{\"type\":\"computer_call\"}"), "unknown output type rejected");
        bad(response("{\"type\":\"message\",\"role\":\"user\",\"content\":[]}"), "wrong role rejected");
        bad(response("{\"type\":\"function_call\",\"name\":\"evolve_action\",\"status\":\"in_progress\",\"arguments\":\"{}\"}"), "in-progress action rejected");
        bad("{\"status\":\"completed\",\"status\":\"failed\",\"output\":[]}", "duplicate root keys rejected");
        bad(response("") , "empty output rejected");
        bad(response(message("okay")) + "garbage", "trailing JSON rejected");
        bad(response(message(std::string(4097, 'x'))), "oversize speech rejected");
        bad(std::string(128 * 1024 + 1, ' '), "oversize response rejected");
        bad("{\"status\":\"completed\",\"output\":[],\"nested\":" + std::string(30, '[') + "0" + std::string(30, ']') + "}", "excessive nesting rejected");
        bad(response(message(std::string("\xed\xa0\x80", 3))), "UTF-8 surrogate rejected");
        bad("{\"status\":\"completed\",\"output\":[{\"type\":\"message\",\"role\":\"assistant\",\"content\":[{\"type\":\"output_text\",\"text\":\"\\ud800\"}]}]}", "unpaired escaped surrogate rejected");
        bad("{\"status\":\"completed\",\"output\":[{\"type\":\"message\",\"role\":\"assistant\",\"content\":[{\"type\":\"output_text\",\"text\":\"\\u0000\"}]}]}", "escaped NUL rejected");
        parsed = parseProviderResponse("{\"status\":\"completed\",\"output\":[{\"type\":\"message\",\"role\":\"assistant\",\"content\":[{\"type\":\"output_text\",\"text\":\"\\ud83d\\ude42\"}]}]}");
        require(parsed.ok() && parsed.text == "🙂", "surrogate pair decoded as UTF-8");
        const std::string refusal = "{\"type\":\"message\",\"role\":\"assistant\",\"content\":[{\"type\":\"refusal\",\"refusal\":\"Cannot help with that.\"}]}";
        require(parseProviderResponse(response(refusal)).ok(), "plain refusal displayed");
        bad(response(refusal + "," + call(args("undo"))), "refusal mixed with action rejected");

        auto followupAction = request("Now change that to G");
        followupAction.conversation.push_back({"Select base 4", "Selected base 4."});
        const auto contextual = buildProviderRequest(followupAction, "gpt-4.1-mini");
        require(contextual.find("Selected base 4.") != std::string::npos && contextual.find("Now change that to G") != std::string::npos, "actual prior action outcome included for follow-up");
        rejects([] { auto r = request(); r.conversation.resize(5, {"Hello", "Hello"}); buildProviderRequest(r, "gpt-4.1-mini"); }, "conversation turn count bounded");
        rejects([] { auto r = request(); r.conversation.push_back({"Hello", std::string(2049, 'x')}); buildProviderRequest(r, "gpt-4.1-mini"); }, "conversation response length bounded");
        rejects([] { auto r = request(); r.conversation.push_back({"Hello", "ACGTACGT"}); buildProviderRequest(r, "gpt-4.1-mini"); }, "history cannot retransmit sequence-like data");
        rejects([] { auto r = request(); r.conversation.push_back({"Hello", "Saved C:\\private\\data.evolve"}); buildProviderRequest(r, "gpt-4.1-mini"); }, "history cannot retransmit paths");

        require(canRetainConversationTurn({"Explain comparison", "The baseline stays unchanged."}), "ordinary conversation can be retained");
        require(!canRetainConversationTurn({"Explain comparison", "First line\nSecond line"}), "multiline history omitted instead of poisoning next request");
        require(!canRetainConversationTurn({"Explain grid", "Use on/off"}), "slash-containing history omitted");
        require(!canRetainConversationTurn({"Explain comparison", std::string(2049, 'x')}), "history helper rejects oversize without truncating");
        auto afterMultiline = request("Explain more");
        const ConversationTurn multiline{"Explain comparison", "First line\nSecond line"};
        if (canRetainConversationTurn(multiline)) afterMultiline.conversation.push_back(multiline);
        require(!buildProviderRequest(afterMultiline, "gpt-4.1-mini").empty(), "follow-up still valid after unsuitable history skipped");

        std::atomic_int calls{};
        std::vector<std::string> sent;
        std::mutex sentMutex;
        ProviderTransport fixture = [&](const std::string& payload, const auto&) {
            ++calls;
            { std::lock_guard<std::mutex> lock(sentMutex); sent.push_back(payload); }
            return ProviderHttpResponse{200, response(message("The comparison view shows both versions.")), {}};
        };
        ConversationalProvider disabled({}, fixture);
        require(!disabled.enabled() && !disabled.busy(), "provider disabled by default");
        const auto disabledId = disabled.submit(request());
        auto result = awaitResult(disabled);
        require(!result.ok() && result.generation == disabledId && calls.load() == 0, "disabled provider never calls transport");
        disabled.setEnabled(true);
        disabled.submit(request("ACGTACGT"));
        result = awaitResult(disabled);
        require(!result.ok() && calls.load() == 0, "privacy rejection happens before transport");
        const auto first = disabled.submit(request());
        result = awaitResult(disabled);
        require(result.ok() && result.generation == first && calls.load() == 1 && !disabled.busy(), "enabled asynchronous fixture completes");
        require(!disabled.poll(), "result can only be consumed once");
        auto followup = request("Can you explain more simply");
        followup.conversation.push_back({"Explain the comparison view", "The comparison view shows both versions."});
        disabled.submit(followup);
        require(awaitResult(disabled).ok(), "second conversational turn completes");
        { std::lock_guard<std::mutex> lock(sentMutex); require(sent.back().find("The comparison view shows both versions.") != std::string::npos, "caller-supplied prior conversation context"); }
        disabled.cancel();
        disabled.submit(request("Explain lighting"));
        require(awaitResult(disabled).ok(), "new request after cancellation completes");
        { std::lock_guard<std::mutex> lock(sentMutex); require(sent.back().find("The comparison view shows both versions.") == std::string::npos, "adapter does not retain hidden conversation context"); }
        disabled.setEnabled(false);
        require(!disabled.enabled() && !disabled.busy() && !disabled.poll(), "disable clears result and state");

        std::promise<void> entered;
        std::promise<void> release;
        auto released = release.get_future().share();
        std::atomic_int sequence{};
        std::atomic_bool sawCancellation{};
        ConversationalProvider newest({true, "gpt-4.1-mini"}, [&](const std::string&, const auto& cancelled) {
            if (++sequence == 1) {
                entered.set_value();
                released.wait();
                sawCancellation = cancelled->load();
                return ProviderHttpResponse{200, response(call(args("load_demo"))), {}};
            }
            return ProviderHttpResponse{200, response(message("Newest response")), {}};
        });
        newest.submit(request("load demo"));
        require(entered.get_future().wait_for(std::chrono::seconds(3)) == std::future_status::ready, "slow fixture starts");
        const auto latest = newest.submit(request("Explain the comparison view"));
        release.set_value();
        result = awaitResult(newest);
        require(result.generation == latest && result.text == "Newest response" && !result.action && sawCancellation.load(), "superseded in-flight action never reaches UI");

        std::promise<void> cancelEntered;
        std::promise<void> cancelReleased;
        auto cancelWait = cancelReleased.get_future().share();
        std::atomic_bool finished{};
        ConversationalProvider cancelled({true, "gpt-4.1-mini"}, [&](const std::string&, const auto& token) {
            cancelEntered.set_value(); cancelWait.wait();
            sawCancellation = token->load(); finished = true;
            return ProviderHttpResponse{200, response(call(args("restore_baseline"))), {}};
        });
        cancelled.submit(request("restore baseline"));
        require(cancelEntered.get_future().wait_for(std::chrono::seconds(3)) == std::future_status::ready, "cancel fixture starts");
        cancelled.setEnabled(false);
        require(!cancelled.busy() && !cancelled.poll(), "disable immediately invalidates pending result");
        cancelReleased.set_value();
        while (!finished.load()) std::this_thread::yield();
        require(!cancelled.poll() && sawCancellation.load(), "disabled in-flight action never reaches UI");

        for (int status : {401, 403, 429, 500, 302}) {
            ConversationalProvider errors({true, "gpt-4.1-mini"}, [=](const std::string&, const auto&) {
                return ProviderHttpResponse{status, "SECRET_SERVER_ERROR_MUST_NOT_BE_DISPLAYED", {}};
            });
            errors.submit(request()); result = awaitResult(errors);
            require(!result.ok() && !result.action && result.error.find("SECRET") == std::string::npos, "HTTP errors do not leak remote bodies");
        }
        ConversationalProvider throws({true, "gpt-4.1-mini"}, [](const std::string&, const auto&) -> ProviderHttpResponse {
            throw std::runtime_error("SECRET_TRANSPORT_EXCEPTION");
        });
        throws.submit(request()); result = awaitResult(throws);
        require(!result.ok() && result.error.find("SECRET") == std::string::npos, "transport exceptions sanitized");
        std::cout << "PASS: " << checks << " provider checks (offline fixtures; no live API calls)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
}
