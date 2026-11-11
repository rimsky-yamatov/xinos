// xasm: XinOS 簡易アセンブラ + VM
// 使い方: xasm <file.xa>
// レジスタ: a b c d / ラベル: "name:" / コメント: ';'
// 命令: mov add sub mul div cmp jmp je jne jl jg push pop call ret out outc halt
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>

namespace {

enum Op { MOV, ADD, SUB, MUL, DIV, CMP, JMP, JE, JNE, JL, JG,
          PUSH, POP, CALL, RET, OUT, OUTC, HALT, kNumOps };

const char* const kNames[kNumOps] = {
  "mov", "add", "sub", "mul", "div", "cmp", "jmp", "je", "jne", "jl", "jg",
  "push", "pop", "call", "ret", "out", "outc", "halt"};

enum Kind { kNone, kReg, kImm, kLabel };
struct Operand { Kind kind; long v; char name[32]; };
struct Insn { Op op; Operand a, b; int line; };
struct Label { char name[32]; int pc; };

constexpr int kMaxInsn = 2048, kMaxLabel = 256, kStack = 1024;
Insn prog[kMaxInsn];
int nprog = 0;
Label labels[kMaxLabel];
int nlabels = 0;
long regs[4], stk[kStack];
int sp = 0;
bool zf = false, lf = false;

bool ParseOperand(char* s, Operand* o) {
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

int FindLabel(const char* n) {
  for (int i = 0; i < nlabels; ++i)
    if (strcmp(labels[i].name, n) == 0) return labels[i].pc;
  return -1;
}

char* Trim(char* s) {
  while (isspace((unsigned char)*s)) ++s;
  char* e = s + strlen(s);
  while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
  return s;
}

bool Assemble(FILE* fp) {
  char buf[256];
  for (int ln = 1; fgets(buf, sizeof buf, fp); ++ln) {
    char* semi = strchr(buf, ';');
    if (semi) *semi = 0;
    char* s = Trim(buf);
    if (!*s) continue;
    char* colon = strchr(s, ':');
    if (colon) {
      *colon = 0;
      if (nlabels >= kMaxLabel) { printf("line %d: too many labels\n", ln); return false; }
      strncpy(labels[nlabels].name, Trim(s), 31);
      labels[nlabels++].pc = nprog;
      s = Trim(colon + 1);
      if (!*s) continue;
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
  }
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

long Val(const Operand& o) { return o.kind == kReg ? regs[o.v] : o.v; }

int Run() {
  long steps = 0;
  for (int pc = 0; pc < nprog;) {
    if (++steps > 50000000) { printf("step limit\n"); return 1; }
    const Insn& in = prog[pc++];
    switch (in.op) {
      case MOV: if (in.a.kind != kReg) goto bad; regs[in.a.v] = Val(in.b); break;
      case ADD: if (in.a.kind != kReg) goto bad; regs[in.a.v] += Val(in.b); break;
      case SUB: if (in.a.kind != kReg) goto bad; regs[in.a.v] -= Val(in.b); break;
      case MUL: if (in.a.kind != kReg) goto bad; regs[in.a.v] *= Val(in.b); break;
      case DIV:
        if (in.a.kind != kReg) goto bad;
        if (Val(in.b) == 0) { printf("line %d: divide by zero\n", in.line); return 1; }
        regs[in.a.v] /= Val(in.b); break;
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

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) { printf("usage: xasm <file.xa>\n"); return 1; }
  FILE* fp = fopen(argv[1], "r");
  if (!fp) { printf("failed to open: %s\n", argv[1]); return 1; }
  bool ok = Assemble(fp);
  fclose(fp);
  return ok ? Run() : 1;
}
