#include "Disasm.h"

#include <cstdio>

namespace crdeadzone {
namespace {

const char* kRegs32[16] = {"eax", "ecx", "edx",  "ebx",  "esp",  "ebp",  "esi",  "edi",
                           "r8d", "r9d", "r10d", "r11d", "r12d", "r13d", "r14d", "r15d"};
const char* kRegs64[16] = {"rax", "rcx", "rdx", "rbx", "rsp",  "rbp",  "rsi",  "rdi",
                           "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15"};

struct Prefixes {
  uint8_t rex = 0;
  bool rexW = false;
  uint8_t regExt = 0;  // REX.R << 3 | (vex not supported: stop)
  uint8_t rmExt = 0;   // REX.B
  uint8_t idxExt = 0;  // REX.X
  size_t len = 0;
  bool operand16 = false;
};

// Reads legacy prefixes + REX. Returns false on VEX/EVEX (unsupported: stop).
bool ReadPrefixes(const uint8_t* p, size_t n, Prefixes& out) {
  size_t i = 0;
  while (i < n) {
    const uint8_t b = p[i];
    if (b == 0xF0 || b == 0xF2 || b == 0xF3 || (b >= 0x26 && b <= 0x3E && b != 0x2E) ||
        b == 0x2E || b == 0x36 || b == 0x64 || b == 0x65 || b == 0x66 || b == 0x67) {
      if (b == 0x66) out.operand16 = true;
      ++i;
      continue;
    }
    if (b >= 0x40 && b <= 0x4F) {
      out.rex = b;
      out.rexW = (b & 0x08) != 0;
      out.regExt = (b & 0x04) ? 8 : 0;
      out.idxExt = (b & 0x02) ? 8 : 0;
      out.rmExt = (b & 0x01) ? 8 : 0;
      ++i;
      continue;
    }
    break;
  }
  if (i < n && (p[i] == 0xC4 || p[i] == 0xC5 || p[i] == 0x62)) return false;  // VEX/EVEX
  out.len = i;
  return true;
}

int32_t ReadI32(const uint8_t* p) {
  int32_t v = 0;
  v |= static_cast<int32_t>(p[0]);
  v |= static_cast<int32_t>(p[1]) << 8;
  v |= static_cast<int32_t>(p[2]) << 16;
  v |= static_cast<int32_t>(p[3]) << 24;
  return v;
}

// Length of ModRM/SIB/displacement tail. Sets ripDisp when mod=00 r/m=101.
// Returns -1 when the encoding needs an opcode-specific immediate the caller
// must add (never: immediates handled by caller via hasImm size).
int ModRmLen(const uint8_t* p, size_t n, bool addr16, bool& ripDisp, int32_t& disp) {
  ripDisp = false;
  if (n < 1) return -1;
  const uint8_t modrm = p[0];
  const uint8_t mod = modrm >> 6, rm = modrm & 7;
  size_t len = 1;
  if (!addr16 && mod != 3) {
    if (rm == 4) {
      if (n < 2) return -1;
      len += 1;  // SIB
      const uint8_t sib = p[1];
      if ((sib & 7) == 5 && mod == 0) {
        if (n < 6) return -1;
        disp = ReadI32(p + 2);
        return 6;
      }
    } else if (rm == 5 && mod == 0) {
      if (n < 5) return -1;
      disp = ReadI32(p + 1);
      ripDisp = true;
      return 5;
    }
    if (mod == 1) {
      if (n < len + 1) return -1;
      return static_cast<int>(len + 1);
    }
    if (mod == 2) {
      if (n < len + 4) return -1;
      return static_cast<int>(len + 4);
    }
    return static_cast<int>(len);
  }
  return static_cast<int>(len);
}

}  // namespace

DecodedInsn DecodeOne(const uint8_t* base, size_t size) {
  DecodedInsn ins;
  if (size == 0) return ins;
  Prefixes pre;
  if (!ReadPrefixes(base, size, pre)) return ins;
  size_t i = pre.len;
  if (i >= size) return ins;
  const uint8_t op = base[i];

  auto need = [&](size_t extra) { return i + extra <= size; };
  char buf[96];

  // ALU/test with accumulator immediate.
  {
    const char* nm = nullptr;
    size_t imm = 0;
    if (op >= 0x04 && op <= 0x3D && ((op & 7) == 4 || (op & 7) == 5)) {
      static const char* kNames[] = {"add", "or", "adc", "sbb", "and", "sub", "xor", "cmp"};
      nm = kNames[(op >> 3) & 7];
      imm = (op & 1) ? (pre.operand16 ? 2 : 4) : 1;
    } else if (op == 0xA8) {
      nm = "test";
      imm = 1;
    } else if (op == 0xA9) {
      nm = "test";
      imm = pre.operand16 ? 2 : 4;
    }
    if (nm) {
      if (!need(1 + imm)) return ins;
      std::snprintf(buf, sizeof(buf), "%s acc,imm", nm);
      ins.text = buf;
      ins.length = static_cast<uint8_t>(i + 1 + imm);
      return ins;
    }
  }
  // mov reg,imm (B0-BF): captures float/int defaults near tunables.
  if (op >= 0xB0 && op <= 0xBF) {
    const size_t imm = (op <= 0xB7) ? 1 : (pre.rexW ? 8 : 4);
    if (!need(1 + imm)) return ins;
    std::snprintf(buf, sizeof(buf), "mov reg,imm%zu", imm * 8);
    ins.text = buf;
    ins.length = static_cast<uint8_t>(i + 1 + imm);
    return ins;
  }
  switch (op) {
    case 0x50:
    case 0x51:
    case 0x52:
    case 0x53:
    case 0x54:
    case 0x55:
    case 0x56:
    case 0x57: {
      std::snprintf(buf, sizeof(buf), "push %s", kRegs64[(op & 7) | pre.rmExt]);
      ins.text = buf;
      ins.length = static_cast<uint8_t>(i + 1);
      return ins;
    }
    case 0x58:
    case 0x59:
    case 0x5A:
    case 0x5B:
    case 0x5C:
    case 0x5D:
    case 0x5E:
    case 0x5F: {
      std::snprintf(buf, sizeof(buf), "pop %s", kRegs64[(op & 7) | pre.rmExt]);
      ins.text = buf;
      ins.length = static_cast<uint8_t>(i + 1);
      return ins;
    }
    case 0x68:  // push imm32
      if (!need(5)) return ins;
      ins.text = "push imm32";
      ins.length = static_cast<uint8_t>(i + 5);
      return ins;
    case 0x6A:  // push imm8
      if (!need(2)) return ins;
      ins.text = "push imm8";
      ins.length = static_cast<uint8_t>(i + 2);
      return ins;
    case 0x70:
    case 0x71:
    case 0x72:
    case 0x73:
    case 0x74:
    case 0x75:
    case 0x76:
    case 0x77:
    case 0x78:
    case 0x79:
    case 0x7A:
    case 0x7B:
    case 0x7C:
    case 0x7D:
    case 0x7E:
    case 0x7F: {
      if (!need(2)) return ins;
      const int8_t d = static_cast<int8_t>(base[i + 1]);
      std::snprintf(buf, sizeof(buf), "jcc %+d", d);
      ins.text = buf;
      ins.length = static_cast<uint8_t>(i + 2);
      return ins;
    }
    case 0xEB: {  // jmp rel8
      if (!need(2)) return ins;
      const int8_t d = static_cast<int8_t>(base[i + 1]);
      std::snprintf(buf, sizeof(buf), "jmp %+d", d);
      ins.text = buf;
      ins.length = static_cast<uint8_t>(i + 2);
      ins.relTarget = static_cast<int64_t>(i + 2 + d);
      return ins;
    }
    case 0xE8: {  // call rel32
      if (!need(5)) return ins;
      const int32_t d = ReadI32(base + i + 1);
      std::snprintf(buf, sizeof(buf), "call %+d", d);
      ins.text = buf;
      ins.length = static_cast<uint8_t>(i + 5);
      ins.isCall = true;
      ins.relTarget = static_cast<int64_t>(i + 5 + d);
      return ins;
    }
    case 0xE9: {  // jmp rel32
      if (!need(5)) return ins;
      const int32_t d = ReadI32(base + i + 1);
      std::snprintf(buf, sizeof(buf), "jmp %+d", d);
      ins.text = buf;
      ins.length = static_cast<uint8_t>(i + 5);
      ins.relTarget = static_cast<int64_t>(i + 5 + d);
      return ins;
    }
    case 0xC3:  // ret
      ins.text = "ret";
      ins.length = static_cast<uint8_t>(i + 1);
      ins.isRet = true;
      return ins;
    case 0xC9:  // leave
      ins.text = "leave";
      ins.length = static_cast<uint8_t>(i + 1);
      return ins;
    case 0x90:  // nop / xchg eax,eax
      ins.text = "nop";
      ins.length = static_cast<uint8_t>(i + 1);
      return ins;
    default:
      break;
  }

  // Two-byte opcodes.
  if (op == 0x0F) {
    if (!need(2)) return ins;
    const uint8_t op2 = base[i + 1];
    if (op2 >= 0x80 && op2 <= 0x8F) {  // jcc rel32
      if (!need(6)) return ins;
      std::snprintf(buf, sizeof(buf), "jcc %+d", ReadI32(base + i + 2));
      ins.text = buf;
      ins.length = static_cast<uint8_t>(i + 6);
      return ins;
    }
    if (op2 == 0x1F) {  // nop r/m (multi-byte nop)
      bool rip = false;
      int32_t disp = 0;
      const int mlen = ModRmLen(base + i + 2, size - i - 2, false, rip, disp);
      if (mlen < 0) return ins;
      ins.text = "nop";
      ins.length = static_cast<uint8_t>(i + 2 + mlen);
      return ins;
    }
    if (op2 == 0xB6 || op2 == 0xB7 || op2 == 0xBE || op2 == 0xBF) {  // movzx
      bool rip = false;
      int32_t disp = 0;
      const int mlen = ModRmLen(base + i + 2, size - i - 2, false, rip, disp);
      if (mlen < 0) return ins;
      ins.text = "movzx";
      ins.length = static_cast<uint8_t>(i + 2 + mlen);
      if (rip) {
        ins.hasRipRef = true;
        ins.ripTarget = static_cast<int64_t>(i + 2 + mlen + disp);
      }
      return ins;
    }
    if (op2 == 0xB0 || op2 == 0xB1) {  // cmpxchg
      bool rip = false;
      int32_t disp = 0;
      const int mlen = ModRmLen(base + i + 2, size - i - 2, false, rip, disp);
      if (mlen < 0) return ins;
      ins.text = "cmpxchg";
      ins.length = static_cast<uint8_t>(i + 2 + mlen);
      return ins;
    }
    return ins;  // unknown 0F xx: stop
  }

  // ModRM group: 0x80/0x81/0x83 (alu r/m,imm), 0x84/0x85 test, 0x88-0x8B mov,
  // 0x8D lea, 0x8F pop r/m, 0xC6/0xC7 mov r/m,imm, 0xF6/0xF7 test/not/neg/...,
  // 0xFE/0xFF inc/dec/call/jmp/push r/m, 0x38-0x3B cmp, 0x30-0x33 xor,
  // 0x00-0x03 add, 0x08-0x0B or, 0x20-0x23 and, 0x28-0x2B sub.
  const bool isModRmOp = op <= 0x03 || (op >= 0x08 && op <= 0x0B) ||
                         (op >= 0x20 && op <= 0x23) || (op >= 0x28 && op <= 0x2B) ||
                         (op >= 0x30 && op <= 0x33) || (op >= 0x38 && op <= 0x3B) ||
                         (op >= 0x84 && op <= 0x8B) || op == 0x8D || op == 0x8F ||
                         op == 0xC6 || op == 0xC7 || op == 0x80 || op == 0x81 ||
                         op == 0x83 || op == 0xF6 || op == 0xF7 || op == 0xFE || op == 0xFF;
  if (!isModRmOp) return ins;
  bool rip = false;
  int32_t disp = 0;
  const int mlen = ModRmLen(base + i + 1, size - i - 1, false, rip, disp);
  if (mlen < 0) return ins;
  size_t total = i + 1 + static_cast<size_t>(mlen);
  const uint8_t modrm = base[i + 1];
  if (op == 0x81 || op == 0xC7) total += 4;
  else if (op == 0x83 || op == 0x80 || op == 0xC6) total += 1;
  else if (op == 0xF6 && ((modrm >> 3) & 7) <= 1) total += 1;
  else if (op == 0xF7 && ((modrm >> 3) & 7) <= 1) total += 4;
  if (total > size) return ins;

  const char* nm = "?";
  if (op <= 0x03) nm = "add";
  else if (op <= 0x0B) nm = "or";
  else if (op <= 0x23) nm = "and";
  else if (op <= 0x2B) nm = "sub";
  else if (op <= 0x33) nm = "xor";
  else if (op <= 0x3B) nm = "cmp";
  else if (op == 0x84 || op == 0x85) nm = "test";
  else if (op >= 0x88 && op <= 0x8B) nm = "mov";
  else if (op == 0x8D) nm = "lea";
  else if (op == 0x8F) nm = "pop";
  else if (op == 0xC6 || op == 0xC7) nm = "mov";
  else if (op == 0x80 || op == 0x81 || op == 0x83) nm = "alu";
  else if (op == 0xF6 || op == 0xF7) nm = "test";
  else if (op == 0xFE) nm = "inc/dec";
  else if (op == 0xFF) nm = ((modrm >> 3) & 7) >= 4 ? "jmp*" : "call*";

  const bool w64 = pre.rexW && op != 0x8D;
  const char* const* regs = w64 ? kRegs64 : kRegs32;
  const uint8_t reg = ((modrm >> 3) & 7) | pre.regExt;
  if (op == 0xFF && ((modrm >> 3) & 7) >= 2) {
    // Indirect call/jmp: target unknowable statically.
    std::snprintf(buf, sizeof(buf), "%s", nm);
    ins.text = buf;
    ins.length = static_cast<uint8_t>(total);
    ins.stopHere = true;
    return ins;
  }
  std::snprintf(buf, sizeof(buf), "%s %s,[mem]", nm, regs[reg]);
  if (rip) {
    char rb[64];
    std::snprintf(rb, sizeof(rb), "%s %s,[rip%+d]", nm, regs[reg],
                  static_cast<int>(disp));
    ins.text = rb;
    ins.hasRipRef = true;
    ins.ripTarget = static_cast<int64_t>(total + disp);
  } else {
    ins.text = buf;
  }
  ins.length = static_cast<uint8_t>(total);
  return ins;
}

FlowInfo WalkFlow(const uint8_t* start, size_t maxBytes, size_t available) {
  FlowInfo flow;
  size_t off = 0;
  const size_t limit = maxBytes < available ? maxBytes : available;
  while (off < limit) {
    DecodedInsn ins = DecodeOne(start + off, limit - off);
    if (ins.length == 0) {
      flow.truncated = true;
      break;
    }
    ins.offset = off;
    if (ins.isCall) {
      flow.callTargets.push_back(static_cast<size_t>(static_cast<int64_t>(off) + ins.relTarget));
    }
    if (ins.hasRipRef) {
      flow.dataRefs.push_back(static_cast<size_t>(static_cast<int64_t>(off) + ins.ripTarget));
    }
    flow.insns.push_back(ins);
    off += ins.length;
    if (ins.isRet || ins.stopHere) {
      flow.truncated = ins.stopHere;
      break;
    }
  }
  return flow;
}

}  // namespace crdeadzone
