// XinOS 簡易アセンブリ(xasm)のアセンブラとVM。xasm と xhl で共有するヘッダオンリー実装。
//
// レジスタ: a b c d    ラベル: "name:"    コメント: ';'
// 命令:
//   mov add sub mul div mod dst, src     算術(dst はレジスタ)
//   cmp x, y                              比較(フラグ設定)
//   jmp je jne jl jg label                分岐
//   push x / pop reg / call label / ret
//   ld reg, addr                          reg = mem[addr]
//   st addr, x                            mem[addr] = x
//   out x (整数+改行) / outc x (1文字) / halt
#pragma once

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace xvm {

enum Op { MOV, ADD, SUB, MUL, DIV, MOD, CMP, JMP, JE, JNE, JL, JG,
          PUSH, POP, CALL, RET, LD, ST, OUT, OUTC, HALT, kNumOps };

inline const char* const kNames[kNumOps] = {
  "mov", "add", "sub", "mul", "div", "mod", "cmp", "jmp", "je", "jne", "jl",
  "jg", "push", "pop", "call", "ret", "ld", "st", "out", "outc", "halt"};

enum Kind { kNone, kReg, kImm, kLabel };
struct Operand { Kind kind; long v; char name[32]; };
struct Insn { Op op; Operand a, b; int line; };
struct Label { char name[32]; int pc; };

constexpr int kMaxInsn = 8192, kMaxLabel = 1024, kStack = 1024, kMemWords = 8192;

inline Insn prog[kMaxInsn];
inline int nprog = 0;
inline Label labels[kMaxLabel];
inline int nlabels = 0;
inline long regs[4], stk[kStack], mem[kMemWords];
inline int sp = 0;
inline bool zf = false, lf = false;

inline bool ParseOperand(char* s, Operand* o) {
  o->kind = kNone; o->v = 0; o->name[0] = 0;
  if (!s || !*s) return true;
  if (s[1] == 0 && s[0] >= 'a' && s[0] <= 'd') {
    o->kind = kReg; o->v = s[0] - 'a'; return true;
  }
  if (isdigit((unsigned char)s[0]) || s[0] == '-') {
    o->kind = kImm; o->v = strtol(s, nullptr, 0); return true;
  }
  if (s[0] == '\'' && s[1] && s[2] == '\'') {
    o->kind = kImm; o->v = (unsigned char)s[1]; return true;
  }
  if (isalpha((unsigned char)s[0]) || s[0] == '_') {
    o->kind = kLabel; strncpy(o->name, s, 31); o->name[31] = 0; return true;
  }
  return false;
}

inline int FindLabel(const char* n) {
  for (int i = 0; i < nlabels; ++i)
    if (strcmp(labels[i].name, n) == 0) return labels[i].pc;
  return -1;
}

inline char* Trim(char* s) {
  while (isspace((unsigned char)*s)) ++s;
  char* e = s + strlen(s);
  while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
  return s;
}

inline void Reset() {
  nprog = 0; nlabels = 0; sp = 0; zf = lf = false;
  for (long& r : regs) r = 0;
  for (long& m : mem) m = 0;
}

// 1行(破壊的に書き換える)をアセンブルする。
inline bool AssembleLine(char* buf, int ln) {
  char* semi = strchr(buf, ';');
  if (semi) *semi = 0;
  char* s = Trim(buf);
  if (!*s) return true;
  char* colon = strchr(s, ':');
  if (colon && s[0] != '\'') {
    *colon = 0;
    if (nlabels >= kMaxLabel) { printf("line %d: too many labels\n", ln); return false; }
    strncpy(labels[nlabels].name, Trim(s), 31);
    labels[nlabels++].pc = nprog;
    s = Trim(colon + 1);
    if (!*s) return true;
  }
  char* sp_ = s;
  while (*sp_ && !isspace((unsigned char)*sp_)) ++sp_;
  char* rest = nullptr;
  if (*sp_) { *sp_ = 0; rest = Trim(sp_ + 1); }
  int op = -1;
  for (int i = 0; i < kNumOps; ++i)
    if (strcmp(s, kNames[i]) == 0) { op = i; break; }
  if (op < 0) { printf("line %d: unknown op '%s'\n", ln, s); return false; }
  if (nprog >= kMaxInsn) { printf("line %d: program too big\n", ln); return false; }
  char *a = rest, *b = nullptr;
  if (rest) {
    char* comma = strchr(rest, ',');
    if (comma) { *comma = 0; b = Trim(comma + 1); a = Trim(rest); }
  }
  Insn& in = prog[nprog++];
  in.op = static_cast<Op>(op); in.line = ln;
  if (!ParseOperand(a, &in.a) || !ParseOperand(b, &in.b)) {
    printf("line %d: bad operand\n", ln); return false;
  }
  return true;
}

// ラベル参照を解決する。全行のアセンブル後に呼ぶ。
inline bool Link() {
  for (int i = 0; i < nprog; ++i) {
    Operand* ops[2] = {&prog[i].a, &prog[i].b};
    for (Operand* o : ops) {
      if (o->kind != kLabel) continue;
      int pc = FindLabel(o->name);
      if (pc < 0) { printf("line %d: undefined label '%s'\n", prog[i].line, o->name); return false; }
      o->kind = kImm; o->v = pc;
    }
  }
  return true;
}

inline bool AssembleFile(FILE* fp) {
  char buf[256];
  for (int ln = 1; fgets(buf, sizeof buf, fp); ++ln)
    if (!AssembleLine(buf, ln)) return false;
  return Link();
}

inline long Val(const Operand& o) { return o.kind == kReg ? regs[o.v] : o.v; }

inline int Run() {
  long steps = 0;
  for (int pc = 0; pc < nprog;) {
    if (++steps > 200000000) { printf("step limit\n"); return 1; }
    const Insn& in = prog[pc++];
    switch (in.op) {
      case MOV: if (in.a.kind != kReg) goto bad; regs[in.a.v] = Val(in.b); break;
      case ADD: if (in.a.kind != kReg) goto bad; regs[in.a.v] += Val(in.b); break;
      case SUB: if (in.a.kind != kReg) goto bad; regs[in.a.v] -= Val(in.b); break;
      case MUL: if (in.a.kind != kReg) goto bad; regs[in.a.v] *= Val(in.b); break;
      case DIV:
      case MOD:
        if (in.a.kind != kReg) goto bad;
        if (Val(in.b) == 0) { printf("line %d: divide by zero\n", in.line); return 1; }
        if (in.op == DIV) regs[in.a.v] /= Val(in.b); else regs[in.a.v] %= Val(in.b);
        break;
      case CMP: { long x = Val(in.a), y = Val(in.b); zf = x == y; lf = x < y; break; }
      case JMP: pc = Val(in.a); break;
      case JE:  if (zf) pc = Val(in.a); break;
      case JNE: if (!zf) pc = Val(in.a); break;
      case JL:  if (lf) pc = Val(in.a); break;
      case JG:  if (!zf && !lf) pc = Val(in.a); break;
      case PUSH:
        if (sp >= kStack) { printf("line %d: stack overflow\n", in.line); return 1; }
        stk[sp++] = Val(in.a); break;
      case POP:
        if (in.a.kind != kReg) goto bad;
        if (sp <= 0) { printf("line %d: stack underflow\n", in.line); return 1; }
        regs[in.a.v] = stk[--sp]; break;
      case CALL:
        if (sp >= kStack) { printf("line %d: stack overflow\n", in.line); return 1; }
        stk[sp++] = pc; pc = Val(in.a); break;
      case RET:
        if (sp <= 0) return 0;
        pc = stk[--sp]; break;
      case LD: {
        if (in.a.kind != kReg) goto bad;
        long addr = Val(in.b);
        if (addr < 0 || addr >= kMemWords) { printf("line %d: bad address %ld\n", in.line, addr); return 1; }
        regs[in.a.v] = mem[addr]; break;
      }
      case ST: {
        long addr = Val(in.a);
        if (addr < 0 || addr >= kMemWords) { printf("line %d: bad address %ld\n", in.line, addr); return 1; }
        mem[addr] = Val(in.b); break;
      }
      case OUT:  printf("%ld\n", Val(in.a)); break;
      case OUTC: printf("%c", (char)Val(in.a)); break;
      case HALT: return 0;
      default: break;
    }
    continue;
  bad:
    printf("line %d: destination must be a register\n", in.line);
    return 1;
  }
  return 0;
}

}  // namespace xvm
