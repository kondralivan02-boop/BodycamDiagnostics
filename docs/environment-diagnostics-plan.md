# Environment diagnostics implementation plan

Goal: preserve useful diagnostics on normal and restricted PCs without elevation or bypasses.

## Block 1: capabilities and Windows Event Log
- Add per-source status, Win32 error and observed evidence; distinguish empty, denied, missing, partial, error and untested.
- Probe process access and Application/System channels using the current token. Record actual capture startup outcomes without claiming successful startup proves complete capture.
- Query bounded local events after capture, preserve UTC time/provider/ID/record ID and XML evidence. Limit to 256 entries/channel, 64 KiB/event and a 2 s enumeration budget/channel.
- Correlate with estimated PresentMon first-frame alignment and a 2 s window around the entire stall; label temporal associations as non-causal. Disable associations on detected wall-clock discontinuity.
- Keep optional telemetry absence as a warning. Preserve invalidation for missing frame/video capture and early game exit.
- Include reports in ZIP, mirror and unified summary. Test interval boundaries, empty/partial sources, JSON escaping and validation behavior. Commit directly to main; inspect Windows build and test logs.

## Block 2: optional Bodycam logs
- Read only known LOCALAPPDATA Bodycam/Saved/Logs, with file/count/total caps and explicit absence/access statuses.
- Collect on normal stops too, retain recent files as optional evidence without claiming per-line clock correlation.
- Verify tests and Windows Actions after the block.

Review focus: no matches != denied; enumeration can fail after partial success; local/UTC clock mismatch; bounds apply to long stalls; optional data must not change capture validity.
