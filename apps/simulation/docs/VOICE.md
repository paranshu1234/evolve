# Voice Agent: experimental desktop controls

The **Voice Agent** companion window adds real local microphone transcription,
optional local spoken replies, and typed commands to the Windows prototype.
Voice is disabled at startup. The local command mode recognizes a fixed set of
English commands; it is not a conversational model or a biological analysis
service. The separate OpenAI conversation option is off by default.

## Start locally

1. Open **Voice Agent** in the desktop toolbar. Type `help` and choose **Send**
   to see the command list. Typed local commands need no microphone, API key,
   internet connection, or installed speech recognizer.
2. Choose **Enable local microphone**. This loads an installed Microsoft
   US-English desktop SAPI recognizer but leaves audio input inactive. If the
   required recognizer is missing, the panel shows an error; typed commands
   remain available. The app does not install speech components automatically.
3. Hold the **Talk** button with the mouse, or focus it and hold **Space**.
   Speak one command clearly. Wait until the listening status appears before
   speaking. Release to close the microphone and submit the recognized text.
4. Recognition may take a moment to finish after release. The displayed
   transcript is what the command parser or selected conversation provider
   receives. Check it, especially for base numbers and A/C/G/T letters.
5. With local voice enabled, **Speak replies** optionally reads replies through
   an installed Microsoft US-English desktop SAPI voice. Recognition and
   synthesis do not run together. Starting another hold interrupts a reply.
6. **Stop** or **Escape** cancels listening, queued speech, pending provider
   results, and pending action approvals. Switching away from the companion
   window also cancels pending work. Closing it disables the microphone and
   cloud mode. **Disable microphone** leaves typed commands available.

Push-to-talk has a 30-second capture limit. After release, the adapter accepts
already-buffered final results for at most two seconds, then purges the stream.
If stream completion is not received before that deadline, the whole utterance
is discarded instead of dispatching a potentially incomplete command prefix.
Slow engine/device calls can add Windows-dependent latency. There is no wake
word, background listening, continuous conversation, or hands-free voice
approval. Very short holds, ambient noise, unavailable devices, and missing
speech components can produce no transcript. Releasing before the end of speech
can truncate it; retry or use typed input.

## Local command examples

Use one complete command at a time. Matching is intentionally narrow: unknown
phrases, command chaining, file paths, arbitrary code, and unsupported actions
are rejected. Base positions are one-based and must fit the current sequence.

| Purpose | Examples |
|---|---|
| Help and context | `help`, `describe project` |
| Select and edit | `select base 12`, `edit base 12 to C` |
| Undoable changes | `undo`, `redo`, `restore baseline` |
| Workspace files | `load demo`, `import DNA`, `open project`, `save project`, `export results` |
| View | `compare on`, `compare off`, `grid on`, `grid off`, `rotation on`, `rotation off`, `frame all` |
| Lighting | `set lighting to 100` (integer 20–160) |
| Mock job | `run analysis`, `cancel analysis` |

Reset/demo and file actions require **Approve exact action** in the panel.
The native file dialogs, overwrite warnings, and unsaved-change prompts still
apply. A spoken “yes” does not approve an action. Reject, Stop, a new request,
or a changed project invalidates an older approval. Ordinary virtual base
edits and undo/redo use the same project controller as the existing buttons.

The local parser is limited to 256 ASCII bytes. The microphone adapter combines
final phrases from one hold and rejects output exceeding 4096 UTF-8 bytes rather
than silently truncating it into a different command. Normal terminal dictation
punctuation is tolerated by the local parser. Spoken numbers and individual
letters are recognizer-dependent; the visible transcript is the useful debugging
evidence.

## Optional OpenAI conversation

The **Use OpenAI conversation (paid API)** checkbox presents a separate
disclosure and requires an affirmative choice for the current window session.
The provider is disabled by default. Its runtime requires the user's own
`OPENAI_API_KEY` in the process environment; the application does not save the
key, create an account, buy credits, or configure billing. Do not put credentials
in project files, source code, screenshots, logs, or bug reports. API use can
incur charges separate from a ChatGPT subscription.

When enabled, sending a typed or locally recognized utterance transmits that
text and a restricted numeric/status summary to OpenAI: sequence length, changed
base count, GC percentage, undo/redo availability, and mock-analysis status.
Up to four suitable completed conversation turns are also included so follow-up requests can refer to prior work. Tool turns use actual local execution results, not model-claimed success. Unsuitable or overlong turns are omitted rather than truncated; conversation memory clears when cloud mode closes/disables or the project is replaced.

Raw sequences, filenames, paths, and project titles are not automatically
included. However, anything the user types or dictates is part of the submitted
text. Do not include private biological data unless sharing it is intended.

The microphone adapter does not send audio. Cloud mode is a text conversation
integration, with optional local SAPI playback of the answer, rather than a
realtime cloud-audio session. Provider replies are proposals; they cannot call
arbitrary functions, choose file paths, approve their own actions, or bypass the
same bounded action validator and project revision checks used locally. Unknown
or malformed output fails closed. A model-generated claim of success is not
used as evidence that a project action happened.

Stopping cancels future handling of a pending reply. A request already sent can
still have been processed or billed by the service; cancellation cannot retract
data already transmitted. Cloud requests and live provider billing have not
been exercised by an offline test suite. Review the current OpenAI API terms,
data controls, and account pricing before opting in.

## Local privacy and implementation boundary

- `WindowsVoice` is a Windows-only SAPI adapter. It selects Microsoft US-English
  desktop recognizer/voice tokens, instead of inheriting an arbitrary third-party
  default engine. Availability is checked at runtime and is not guaranteed on
  every Windows 10/11 installation.
- Construction and opening the companion window do not create a recognizer or
  open the microphone. `initialize()` follows explicit enablement and configures
  an inactive in-process recognizer. Only push-to-talk sets it active.
- `SPRST_INACTIVE` closes input while finalizing buffered recognition.
  Stop/cancellation uses `SPRST_INACTIVE_WITH_PURGE`. The app owns a private
  recognizer, so its state changes do not stop another application's shared
  speech recognizer.
- `SPAO_NONE` disables retained audio in recognition results. The adapter has no
  audio-file writer or network transport. Transcripts/replies are kept in bounded
  panel memory and are not added to `.evolve` project files. Windows speech
  profiles and OS-level diagnostics have their own settings outside this app.
- All adapter methods and destruction run on the owning UI/COM thread. COM
  interfaces, event payloads, and allocated phrase/text buffers use scoped
  cleanup. Shutdown releases interfaces before balancing COM initialization.
- Each hold creates a fresh recognition context and a grammar with a unique
  session ID. Recognition must match that ID. Stop increments the session,
  discards accumulated text, drains events, and releases the context. The UI
  additionally matches the microphone session to its project/request ticket.
- A failed stop closes the recognizer/input objects and enters an error state;
  it does not report a reassuring ready state. Error messages include an HRESULT
  to help diagnose missing components or device failure without logging audio.
- Text-to-speech uses `SPF_IS_NOT_XML`, so reply text cannot supply SAPI markup
  that changes voices, reads files, or injects synthesis instructions.

The app remains a schematic sequence-editing prototype. Composition percentages
and edit counts are descriptive, and analysis is explicitly mock. Neither voice
mode provides biological-effect prediction, medical interpretation, or physical
genome editing.

## Build and verification gates

Build with the Windows C++ toolchain described in the
[desktop README](../README.md). The native voice source requires Windows SDK
`sapi.h` and the SAPI/COM libraries (`sapi`, `ole32`; Windows GUID linkage where
required by the toolchain). The portable command/provider validation tests do not
load SAPI and can run on Linux. Successful parser tests do not establish that a
microphone or speaker works.

From `apps/simulation` on Windows:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
.\build\Release\Evolve.exe --warp --smoke-test
```

The renderer smoke test must not enable a microphone or perform a paid network
request. After it passes, run these separate manual voice gates on a Windows
machine with a real input/output device. Record the OS build, installed speech
language, microphone, application commit, and actual result for each gate.

1. **Default privacy:** launch and open Voice Agent. Confirm microphone/cloud are
   off and typed `help` works with no API key and with the network disconnected.
2. **Enable without capture:** enable local microphone. Confirm ready/mic-off
   status and Windows microphone activity begins only while Talk is held.
3. **Real recognition:** test mouse hold and keyboard Space separately with
   `describe project`, `select base 3`, `grid off`, and `grid on`. Verify the real
   transcript and the corresponding workspace state. Do not substitute typed
   commands or emulated recognition for this microphone gate.
4. **Release/finalization:** release after a complete phrase, release mid-word,
   perform a silent hold, and reach the 30-second limit. Confirm input closes,
   status recovers, and a hold produces at most one dispatch.
5. **Cancellation and stale events:** Stop while speaking, release immediately
   after Stop, switch applications during a hold/finalization, and rapidly start
   a new hold. No cancelled transcript may execute in a later session.
6. **Project changes:** change the sequence, load another project, or change the
   save destination while a request/approval is pending. Delayed actions must
   be rejected against the new project/request state.
7. **Confirmation:** request `restore baseline`, reject once, request it again,
   then approve. Test Save/Open/Import/Export with cancelled native dialogs and
   overwrite prompts. Confirm a spoken “yes” cannot authorize them.
8. **Speech output:** enable Speak replies and verify audible local playback.
   Stop and a new hold must interrupt it; microphone input must remain inactive
   during playback. Verify plain-text handling using markup-like typed content
   in a controlled local test, with no file access.
9. **Failure recovery:** deny Windows desktop-app microphone access, disconnect
   the input device, and test a machine without the US-English SAPI recognizer
   or TTS voice. Confirm actionable failure, no stuck listening indicator,
   typed-command availability, and recovery through explicit re-enablement.
10. **Close/reopen:** close the panel during recognition/speech, reopen it, and
    close the application. Confirm no microphone remains active and cloud
    opt-in is not silently restored on panel reopening.
11. **Opt-in cloud, separate authorization:** only with an intentionally
    configured API account and consent, verify the disclosure, allowed payload,
    actual model response, rejected malformed actions, provider failure, Stop,
    and session close. This can incur API charges and must not be included in
    default CI or represented as passed by mocked transport tests.

Native SAPI microphone/TTS runtime gates are **pending until actual Windows
evidence is recorded**. Source compilation, WARP rendering, offline fixtures,
and actual microphone/provider operation are separate claims.

### Microsoft API references

- [Speech API overview](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ms720151(v=vs.85))
- [Recognizer states and input closure](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ms717269(v=vs.85))
- [Grammar result identifiers](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ms718494(v=vs.85))
- [Disabling retained result audio](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee413255(v=vs.85))
- [Installed-token attributes](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ms718134(v=vs.85))
