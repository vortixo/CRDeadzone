// Support specs: the arbitration clock rule (pure, no test hooks in
// production code), the pattern scanner on synthetic buffers, the
// anchor-flow disassembler, and the logger's output format.

#include "Test.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Activity.h"
#include "Disasm.h"
#include "Logger.h"
#include "PatternScanner.h"

namespace {

using namespace crdeadzone;

void Arbitration() {
  // Never marked: idle at any timestamp.
  EXPECT_TRUE(!IsRecentlyActive(0, 1000000));
  // Just marked: live.
  EXPECT_TRUE(IsRecentlyActive(10000, 10000));
  EXPECT_TRUE(IsRecentlyActive(10000, 11999));
  // The window is [last, last + 2000): the edge itself is expired.
  EXPECT_TRUE(!IsRecentlyActive(10000, 12000));
  // A clock moving backwards never reads as active.
  EXPECT_TRUE(!IsRecentlyActive(200, 100));
}

void ScannerParse() {
  const auto bytes = PatternScanner::Parse("48 8D ?? 0D");
  EXPECT_EQ(bytes.size(), size_t{4});
  EXPECT_TRUE(!bytes[0].wildcard && bytes[0].value == 0x48);
  EXPECT_TRUE(bytes[2].wildcard);
  EXPECT_TRUE(!bytes[3].wildcard && bytes[3].value == 0x0D);
  // Single-question wildcard, lowercase hex, and sloppy whitespace.
  const auto misc = PatternScanner::Parse("? 4a  8d");
  EXPECT_EQ(misc.size(), size_t{3});
  EXPECT_TRUE(misc[0].wildcard);
  EXPECT_TRUE(!misc[1].wildcard && misc[1].value == 0x4A);
  EXPECT_EQ(PatternScanner::Parse("").size(), size_t{0});
}

void ScannerFind() {
  uint8_t buf[64] = {};
  buf[10] = 0x48;
  buf[11] = 0x8D;
  buf[12] = 0x00;  // arbitrary byte under the wildcard
  buf[13] = 0xAA;
  const PatternScanner sc(buf, sizeof(buf));
  const auto hit = sc.Find("48 8D ?? AA");
  EXPECT_TRUE(hit.has_value() && *hit == buf + 10);
  EXPECT_TRUE(!sc.Find("FF FF FF").has_value());
  EXPECT_TRUE(!sc.Find("").has_value());
  // A pattern longer than the buffer can never match.
  uint8_t tiny[8] = {};
  const PatternScanner small(tiny, sizeof(tiny));
  EXPECT_TRUE(!small.Find("00 00 00 00 00 00 00 00 00").has_value());

  buf[30] = 0x48;
  const auto all = sc.FindAll("48");
  EXPECT_EQ(all.size(), size_t{2});
  EXPECT_EQ(sc.FindAll("48", 1).size(), size_t{1});  // max is honored
}

void StringAnchors() {
  // NUL-terminated match only: "dead" at offset 0 is followed by 'Z', not NUL.
  uint8_t text[] = {'d', 'e', 'a', 'd', 'Z', 'o', 'n', 'e', 0, 'x'};
  const auto hit = FindStringRef(text, sizeof(text), "deadZone");
  EXPECT_TRUE(hit.has_value() && *hit == text);
  EXPECT_TRUE(!FindStringRef(text, sizeof(text), "dead").has_value());
  // A later substring with its own NUL counts: "one" at offset 5.
  const auto sub = FindStringRef(text, sizeof(text), "one");
  EXPECT_TRUE(sub.has_value() && *sub == text + 5);
  EXPECT_TRUE(!FindStringRef(text, sizeof(text), "missing").has_value());
}

void LeaRefs() {
  // Genuine encoding: REX.W + 8D, modrm mod=00/r/m=101, disp = target-(instr+7).
  uint8_t text[] = {'d', 'e', 'a', 'd', 'Z', 'o', 'n', 'e', 0, 'x'};
  uint8_t code[32] = {};
  const uint8_t* target = text;
  auto emitLea = [&](size_t at, uint8_t modrm) {
    code[at] = 0x48;
    code[at + 1] = 0x8D;
    code[at + 2] = modrm;
    const int32_t disp = static_cast<int32_t>(target - (code + at + 7));
    std::memcpy(code + at + 3, &disp, 4);
  };
  emitLea(4, 0x0D);  // LEA rcx,[rip+disp]: canonical form
  const auto refs = FindLeaRefs(code, sizeof(code), target, 8);
  EXPECT_EQ(refs.size(), size_t{1});
  EXPECT_TRUE(!refs.empty() && refs[0] == code + 4);

  // Same displacement with a non-RIP-relative modrm must not match.
  emitLea(16, 0x08);  // mod=00, r/m=000: different addressing, same bytes after
  const auto refs2 = FindLeaRefs(code, sizeof(code), target, 8);
  EXPECT_EQ(refs2.size(), size_t{1});
  EXPECT_TRUE(!refs2.empty() && refs2[0] == code + 4);
}

void Disassembler() {
  // call rel32: E8 <disp>; target = pos + 5 + disp.
  {
    uint8_t code[] = {0xE8, 0xFB, 0x00, 0x00, 0x00, 0xC3};
    const DecodedInsn ins = DecodeOne(code, sizeof(code));
    EXPECT_TRUE(ins.length == 5 && ins.isCall && !ins.isRet);
    EXPECT_EQ(ins.relTarget, int64_t{5 + 0xFB});
    const FlowInfo flow = WalkFlow(code, 16, sizeof(code));
    EXPECT_EQ(flow.callTargets.size(), size_t{1});
    EXPECT_TRUE(!flow.insns.empty() && flow.insns.back().isRet);
  }
  // LEA rcx,[rip+disp]: 48 8D 0D <disp32>.
  {
    uint8_t code[] = {0x48, 0x8D, 0x0D, 0x10, 0x00, 0x00, 0x00, 0x90};
    const DecodedInsn ins = DecodeOne(code, sizeof(code));
    EXPECT_TRUE(ins.length == 7 && ins.hasRipRef);
    EXPECT_EQ(ins.ripTarget, int64_t{7 + 0x10});
    EXPECT_TRUE(ins.text.find("lea") != std::string::npos);
  }
  // push rbp; mov rbp,rsp; pop rbp; ret.
  {
    uint8_t code[] = {0x55, 0x48, 0x89, 0xE5, 0x5D, 0xC3};
    const FlowInfo flow = WalkFlow(code, 16, sizeof(code));
    EXPECT_EQ(flow.insns.size(), size_t{4});
    EXPECT_TRUE(!flow.insns.empty() && flow.insns.back().isRet);
    EXPECT_EQ(flow.insns[1].length, uint8_t{3});
  }
  // SIB + disp32: 48 8B 84 25 <disp32>.
  {
    uint8_t code[] = {0x48, 0x8B, 0x84, 0x25, 0x30, 0x00, 0x00, 0x00};
    EXPECT_EQ(DecodeOne(code, sizeof(code)).length, uint8_t{8});
  }
  // Unknown opcodes stop the walk instead of desyncing it.
  {
    uint8_t code[] = {0x90, 0xF1, 0x90};  // F1 = int1: intentionally unsupported
    const FlowInfo flow = WalkFlow(code, 16, sizeof(code));
    EXPECT_EQ(flow.insns.size(), size_t{1});
    EXPECT_TRUE(flow.truncated);
    EXPECT_EQ(DecodeOne(code + 1, 2).length, uint8_t{0});
  }
  // Indirect call: target unknowable, walk stops.
  {
    uint8_t code[] = {0xFF, 0x15, 0x00, 0x00, 0x00, 0x00};
    const DecodedInsn ins = DecodeOne(code, sizeof(code));
    EXPECT_TRUE(ins.length == 6 && ins.stopHere && !ins.isCall);
  }
  // mov reg,imm32 captures immediates.
  {
    uint8_t code[] = {0xB8, 0x9A, 0x99, 0x19, 0x3E, 0xC3};  // mov eax,0x3E19999A
    const DecodedInsn ins = DecodeOne(code, sizeof(code));
    EXPECT_TRUE(ins.length == 5 && ins.text.find("imm32") != std::string::npos);
  }
  // Empty input decodes to nothing.
  {
    EXPECT_EQ(DecodeOne(nullptr, 0).length, uint8_t{0});
    const FlowInfo flow = WalkFlow(nullptr, 16, 0);
    EXPECT_TRUE(flow.insns.empty() && !flow.truncated);
  }
}

void LogOutput() {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto dir =
      std::filesystem::temp_directory_path() / ("crdeadzone_log_" + std::to_string(stamp));
  std::filesystem::create_directories(dir);
  const std::string narrow = dir.string();
  Logger::Instance().Init(std::wstring(narrow.begin(), narrow.end()));
  Logger::Instance().Info("hello-info");
  Logger::Instance().Warn("hello-warn");
  Logger::Instance().Error("hello-error");
  Logger::Instance().Shutdown();
  EXPECT_TRUE(std::filesystem::exists(dir / "CRDeadzone.log"));
  std::ifstream f(dir / "CRDeadzone.log");
  const std::string content((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());
  EXPECT_TRUE(content.find("[INFO] hello-info") != std::string::npos);
  EXPECT_TRUE(content.find("[WARN] hello-warn") != std::string::npos);
  EXPECT_TRUE(content.find("[ERROR] hello-error") != std::string::npos);
  std::filesystem::remove_all(dir);
}

}  // namespace

void SupportTests() {
  crtest::Suite("arbitration", Arbitration);
  crtest::Suite("scanner-parse", ScannerParse);
  crtest::Suite("scanner-find", ScannerFind);
  crtest::Suite("string-anchors", StringAnchors);
  crtest::Suite("lea-refs", LeaRefs);
  crtest::Suite("disassembler", Disassembler);
  crtest::Suite("logger", LogOutput);
}
