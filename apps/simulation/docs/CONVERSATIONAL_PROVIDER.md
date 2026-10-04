# Conversational cloud provider

## What is implemented

The optional Windows adapter makes a real HTTPS `POST` to
`https://api.openai.com/v1/responses` using WinHTTP. It accepts text produced by the
local speech recognizer or typed in the voice panel, supports short follow-up
conversations, and returns either an answer or one typed Evolve action proposal.
Local Windows speech synthesis speaks the resulting UI response. Audio capture,
recognition and synthesis are separate from this adapter; this is not a Realtime
API or streaming speech implementation.

Cloud conversation is **disabled by default**. Nothing in construction, local
command mode, or the offline tests reads credentials or calls the provider.
An explicit runtime opt-in must follow the panel's data-sharing/cost disclosure.
A user's own API account, key, supported model access and billing are required.
API use may incur charges.

The default model is `gpt-4.1-mini`. The application can supply another Responses
API model with function-calling support in `ProviderConfig.model`; arbitrary
endpoints, URLs and provider-supplied execution tools are not configurable.

## Local setup and consent

1. Provision your own OpenAI API key outside Evolve. Never paste it into voice
   input, a project, source code, a screenshot, a bug report or a chat transcript.
2. Set `OPENAI_API_KEY` in the environment of the Windows process that will launch
   Evolve, using your own secret-management workflow. Restart Evolve after setting
   it. The application does not install, save or print the key.
3. Open the voice panel and review the cloud disclosure before enabling it.
4. Only submit commands/questions you are willing to send to OpenAI. Disable the
   cloud option to return to local-only use.

Environment variables are not a credential vault. The adapter reads the variable
only while executing an enabled request and best-effort scrubs its application
copies of the key/header; Windows and TLS own additional copies. Account/key
provisioning and a paid live request were deliberately not performed during
implementation.

## Data boundary

The request contains:

- the current UTF-8 spoken transcript or typed question, limited to 2,048 bytes;
- up to four caller-supplied completed user/assistant exchanges, each field
  limited to 2,048 bytes;
- numeric/boolean aggregate context: sequence length, edit count, GC percentage,
  undo/redo availability, current analysis availability and analysis-running state;
- fixed Evolve instructions and the allowlisted action schema.

The context type has no fields for sequence letters, files, project titles or
paths. The adapter never reads project files, uploads files or sends audio.
It rejects controls, slash/path/URL-like input, known sequence/project filename
extensions and runs of eight DNA-like letters, including spaced letters. These
are conservative accidental-sharing checks, **not a general-purpose sensitive
information detector**. Do not include sequences, paths, credentials or private
biological information in cloud questions, even if a phrasing passes the checks.

`store:false` is set on every request. This opts out of Responses application
state storage; it does not promise zero retention under every OpenAI account's
data controls. Consult the official [data controls documentation](https://developers.openai.com/api/docs/guides/your-data).
No conversation ID, previous response ID, remote file store, transcript log or
on-disk conversation history is created by this adapter.

The UI supplies actual local execution outcomes as history. A model proposal is
never recorded as a completed operation. `canRetainConversationTurn()` permits
the UI to omit an unsuitable whole pair (for example, a multiline or path-like
answer) instead of poisoning the next request; it does not silently truncate.
Clear UI history on disabling/closing cloud conversation or replacing the project,
and do not add stale/cancelled turns. The provider itself retains no history.

## Action safety

The sole function tool is `evolve_action`, with `strict:true`, closed properties,
required arguments and nullable unused fields. `parallel_tool_calls:false` asks
for at most one call; the local parser independently rejects multiple calls.
Only the following actions exist:

- help / describe project
- select or edit one base, undo, redo, restore baseline, load demo
- import sequence, open/save project, export results through native UI controls
- set compare/grid/rotation explicitly, frame all, set lighting
- run or cancel analysis

There is no shell, script, path, URL, arbitrary sequence payload or confirmation
tool. Spoken base positions are one-based; the typed index is zero-based, bounded
to 0–255 and checked again against the request's project length. Edits accept one
uppercase A/C/G/T. Lighting is an integer from 20 through 160. Unused arguments
must be null and become canonical zero/default fields in the typed action.

JSON parsing is bounded and rejects duplicate keys, invalid UTF-8/escapes,
unpaired surrogates, non-finite numbers, excessive nesting, trailing data,
incomplete responses, unknown tools and unknown argument fields. A refusal mixed
with an action also fails closed. Remote error bodies are never displayed.
When an action is proposed, cloud text is replaced with a neutral local message;
only the UI may announce actual success or a required confirmation.

A parsed action is **not permission to execute it**. The UI must check the
provider generation and its own session/request ticket, project epoch and
revision, then pass the proposal through `Controller`. Destructive resets and
file operations retain the UI's one-use explicit confirmation and native picker /
overwrite handling. Cloud output cannot approve a confirmation.

## Threading and transport

`ConversationalProvider` owns one worker thread and a one-item replaceable pending
queue. `submit()` returns a generation; the owner thread calls `poll()` and
consumes a result once. `cancel()`, disable and newer submissions invalidate old
requests/results immediately. No worker accesses UI objects or invokes UI
callbacks. Public methods belong to one owner/UI thread.

WinHTTP connects only to `api.openai.com:443`, with HTTPS certificate validation,
redirects disabled, cookies disabled and Windows default-credential autologon
disabled. There are no automatic application retries. Resolve/connect timeouts
are five seconds and send/receive timeouts ten seconds; response reads also have
a 30-second overall read deadline and 128 KiB total body cap.

Cancellation cannot retract bytes already sent or reverse API charges. An active
synchronous Windows call is allowed to return/timeout on the worker, then its
result is discarded. The adapter deliberately does not close an in-use synchronous
WinHTTP handle from another thread, which Microsoft documents as unsafe.
Destruction cancels and joins the owned worker and can therefore wait for the
active Windows call to return; cancelling/closing the panel need not destroy the
long-lived provider. The UI must not promise instant network termination.

## Verification

Offline `evolve_provider_tests` covers 125 checks, including request construction,
privacy bounds, follow-up history/outcomes, all allowed actions, malformed JSON,
Unicode, refusals, multiple calls, HTTP failure sanitization, disabled-by-default
behavior, generation supersession and cancellation. All transport calls in this
suite are injected fixtures; no secret is inspected and no network call is made.

The provider translation unit was also cross-compiled for Windows x64 with
Zig/MinGW and `-Wall -Wextra -Werror`. This checks Windows API declarations and C++
compilation, not live transport, Windows speech, microphone permission, playback,
API account access, billing or conversational model quality. Those require a
separately authorized Windows end-to-end test. Linux builds expose the portable
codec/fixtures and report that native live transport requires Windows.

## Implementation references

- [OpenAI function calling](https://developers.openai.com/api/docs/guides/function-calling): Responses function calls, strict schema requirements and parallel-call control.
- [Responses API reference](https://developers.openai.com/api/reference/typescript/resources/beta/subresources/responses/methods/create): request/output envelope and status handling.
- [GPT-4.1 mini](https://developers.openai.com/api/docs/models/gpt-4.1-mini): default model and Responses support.
- [WinHTTP options](https://learn.microsoft.com/en-us/windows/win32/winhttp/option-flags): redirect and automatic-logon policies.
- [WinHTTP concurrency](https://learn.microsoft.com/en-us/windows/win32/winhttp/concurrency-in-winhttp): synchronous handle lifetime/cancellation restrictions.
