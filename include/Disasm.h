#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Minimal x86-64 decoder for anchor data-flow logging. It exists for one job:
// starting at a string reference (e.g. LEA of "deadZone"), walk forward and
// report CALL targets and RIP-relative data refs as RVAs (ASLR-proof across
// runs). Length decoding is exact for compiler-generated code; unknown
// opcodes stop the walk instead of desyncing it. Portable and unit tested.

namespace crdeadzone {

struct DecodedInsn {
  size_t offset = 0;      // offset from walk start
  uint8_t length = 0;
  std::string text;       // e.g. "call", "lea rcx,[rip+0x1234]", "mov", "db 0xF1"
  bool isCall = false;
  bool isRet = false;
  bool stopHere = false;  // set for indirect calls/jumps: walk should stop
  int64_t relTarget = 0;  // for rel32 call/jmp: target offset from walk start
  bool hasRipRef = false;
  int64_t ripTarget = 0;  // for RIP-relative: referenced offset from walk start
};

// Decodes one instruction at [base, base+size). Returns length 0 when the
// opcode is unknown (caller stops the walk).
DecodedInsn DecodeOne(const uint8_t* base, size_t size);

struct FlowInfo {
  std::vector<DecodedInsn> insns;
  std::vector<size_t> callTargets;  // offsets from walk start
  std::vector<size_t> dataRefs;     // offsets from walk start
  bool truncated = false;
};

// Walks at most maxBytes from start, decoding sequentially.
FlowInfo WalkFlow(const uint8_t* start, size_t maxBytes, size_t available);

}  // namespace crdeadzone
