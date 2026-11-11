// xhl: XinOS 簡易高水準言語。xasm のコードを生成してそのままVMで実行する。
//
// 使い方: xhl [-S] <file.xh>      (-S は生成したアセンブリを表示するだけ)
//
// 言語:
//   // コメント
//   var x = 1;  x = x * 2 + 3;
//   printf("x = %d, %c, %s\n", x, 65, "str");   書式: %d %c %s(文字列リテラルのみ) %%
//   scanf("%d", &x);      書式: %d %c と空白。値は読めた個数(失敗なし EOF なら EOF=-1)
//   while (scanf("%d", &x) == 1) { ... }
//   if (a < b) { ... } else if (...) { ... } else { ... }
//   while (cond) { ... }
//   func name() { ... return; }   name();     引数なし、値の受け渡しはグローバル変数で
//   演算子: + - * / %  == != < > <= >=  && ||  ! 単項-   整数と文字リテラル 'a'
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>

#include "../xasm/xvm.hpp"

namespace {

// ---------------- 字句解析 ----------------
enum TokType { T_END, T_NUM, T_ID, T_STR, T_OP };
struct Tok { TokType type; char text[128]; long num; int line; };

char src[1 << 16];
const char* p;
int cur_line = 1;
Tok tok;           // 現在のトークン
bool failed = false;

void Error(const char* fmt, ...) {
  if (failed) return;
  failed = true;
  va_list ap; va_start(ap, fmt);
  printf("xhl: line %d: ", tok.line);
  vprintf(fmt, ap);
  printf("\n");
  va_end(ap);
}

void Next() {
  for (;;) {
    while (*p && isspace((unsigned char)*p)) { if (*p == '\n') ++cur_line; ++p; }
    if (p[0] == '/' && p[1] == '/') { while (*p && *p != '\n') ++p; continue; }
    break;
  }
  tok.line = cur_line; tok.num = 0; tok.text[0] = 0;
  if (!*p) { tok.type = T_END; return; }
  if (isdigit((unsigned char)*p)) {
    tok.type = T_NUM; tok.num = strtol(p, const_cast<char**>(&p), 0); return;
  }
  if (isalpha((unsigned char)*p) || *p == '_') {
    int n = 0;
    while ((isalnum((unsigned char)*p) || *p == '_') && n < 63) tok.text[n++] = *p++;
    tok.text[n] = 0; tok.type = T_ID; return;
  }
  if (*p == '"') {
    ++p; int n = 0;
    while (*p && *p != '"' && n < 126) {
      char c = *p++;
      if (c == '\\' && *p) {
        c = *p++;
        c = c == 'n' ? '\n' : c == 't' ? '\t' : c;
      }
      tok.text[n++] = c;
    }
    if (*p != '"') { Error("unterminated string"); }
    else ++p;
    tok.text[n] = 0; tok.type = T_STR; return;
  }
  if (*p == '\'' && p[1] && p[2] == '\'') {
    tok.type = T_NUM; tok.num = (unsigned char)p[1]; p += 3; return;
  }
  static const char* two[] = {"==", "!=", "<=", ">=", "&&", "||"};
  for (const char* t : two)
    if (p[0] == t[0] && p[1] == t[1]) {
      tok.type = T_OP; tok.text[0] = t[0]; tok.text[1] = t[1]; tok.text[2] = 0; p += 2; return;
    }
  tok.type = T_OP; tok.text[0] = *p++; tok.text[1] = 0;
}

bool IsOp(const char* s) { return tok.type == T_OP && strcmp(tok.text, s) == 0; }
bool IsKw(const char* s) { return tok.type == T_ID && strcmp(tok.text, s) == 0; }
bool Accept(const char* s) { if (IsOp(s)) { Next(); return true; } return false; }
void Expect(const char* s) {
  if (!Accept(s)) Error("expected '%s'", s);
}

// ---------------- コード生成 ----------------
bool dump_only = false;
int emitted = 0, label_n = 0;

void Emit(const char* fmt, ...) {
  if (failed) return;
  char line[160];
  va_list ap; va_start(ap, fmt);
  vsnprintf(line, sizeof line, fmt, ap);
  va_end(ap);
  ++emitted;
  if (dump_only) { printf("%s\n", line); return; }
  if (!xvm::AssembleLine(line, emitted)) failed = true;
}

int NewLabel() { return label_n++; }
void Place(int l) { Emit("L%d:", l); }

struct Var { char name[64]; int addr; };
Var vars[1024]; int nvars = 0;
struct Func { char name[64]; };
Func funcs[256]; int nfuncs = 0;

int FindVar(const char* n) {
  for (int i = 0; i < nvars; ++i) if (strcmp(vars[i].name, n) == 0) return vars[i].addr;
  return -1;
}
int DeclVar(const char* n) {
  if (nvars >= 1024 || nvars >= xvm::kMemWords) { Error("too many variables"); return 0; }
  strncpy(vars[nvars].name, n, 63); vars[nvars].addr = nvars; return nvars++;
}
bool FindFunc(const char* n) {
  for (int i = 0; i < nfuncs; ++i) if (strcmp(funcs[i].name, n) == 0) return true;
  return false;
}

void Norm() {  // a = (a != 0)
  int l = NewLabel();
  Emit("cmp a, 0"); Emit("mov a, 1"); Emit("jne L%d", l); Emit("mov a, 0"); Place(l);
}

void ParseExpr();

// scanf("%d %c", &x, &y) : 読めた個数を a に返す。失敗/EOF で打ち切る。
void ParseScanf() {
  Next(); Expect("(");
  if (tok.type != T_STR) { Error("scanf: format string expected"); return; }
  char fmt[128]; strcpy(fmt, tok.text); Next();
  const int lfail = NewLabel(), lend = NewLabel();
  Emit("mov d, 0");
  for (const char* f = fmt; *f && !failed; ++f) {
    if (isspace((unsigned char)*f)) { Emit("skipws"); continue; }
    if (*f != '%') { Error("scanf: literal '%c' in format is not supported", *f); return; }
    ++f;
    if (*f != 'd' && *f != 'c') { Error("scanf: unsupported conversion '%%%c'", *f ? *f : ' '); return; }
    const char conv = *f;
    Expect(","); Expect("&");
    if (tok.type != T_ID) { Error("scanf: variable expected after '&'"); return; }
    int addr = FindVar(tok.text);
    if (addr < 0) { Error("undefined variable '%s'", tok.text); return; }
    Next();
    Emit(conv == 'd' ? "in a" : "inch a");
    Emit("inok b"); Emit("cmp b, 1"); Emit("jne L%d", lfail);
    Emit("st %d, a", addr); Emit("add d, 1");
  }
  Expect(")");
  Emit("jmp L%d", lend);
  Place(lfail);
  Emit("cmp d, 0"); Emit("jne L%d", lend);
  Emit("cmp b, -1"); Emit("jne L%d", lend);
  Emit("mov d, -1");
  Place(lend);
  Emit("mov a, d");
}

void ParsePrimary() {
  if (tok.type == T_NUM) { Emit("mov a, %ld", tok.num); Next(); return; }
  if (tok.type == T_ID && strcmp(tok.text, "scanf") == 0) { ParseScanf(); return; }
  if (tok.type == T_ID) {
    int addr = FindVar(tok.text);
    if (addr < 0 && strcmp(tok.text, "EOF") == 0) { Emit("mov a, -1"); Next(); return; }
    if (addr < 0) { Error("undefined variable '%s'", tok.text); return; }
    Emit("ld a, %d", addr); Next(); return;
  }
  if (Accept("(")) { ParseExpr(); Expect(")"); return; }
  Error("unexpected token in expression");
}

void ParseUnary() {
  if (Accept("-")) { ParseUnary(); Emit("mul a, -1"); return; }
  if (Accept("!")) {
    ParseUnary();
    int l = NewLabel();
    Emit("cmp a, 0"); Emit("mov a, 1"); Emit("je L%d", l); Emit("mov a, 0"); Place(l);
    return;
  }
  ParsePrimary();
}

// 二項演算の右辺を評価して a=左, b=右 にする
void Binary(void (*rhs)()) {
  Emit("push a"); rhs(); Emit("mov b, a"); Emit("pop a");
}

void ParseMul() {
  ParseUnary();
  while (!failed && (IsOp("*") || IsOp("/") || IsOp("%"))) {
    const char* op = IsOp("*") ? "mul" : IsOp("/") ? "div" : "mod";
    Next(); Binary(ParseUnary); Emit("%s a, b", op);
  }
}
void ParseAdd() {
  ParseMul();
  while (!failed && (IsOp("+") || IsOp("-"))) {
    const char* op = IsOp("+") ? "add" : "sub";
    Next(); Binary(ParseMul); Emit("%s a, b", op);
  }
}
void ParseCmp() {
  ParseAdd();
  while (!failed && (IsOp("<") || IsOp(">") || IsOp("<=") || IsOp(">="))) {
    char op[3]; strcpy(op, tok.text);
    Next(); Binary(ParseAdd);
    int l = NewLabel();
    Emit("cmp a, b"); Emit("mov a, 1");
    if (!strcmp(op, "<")) Emit("jl L%d", l);
    else if (!strcmp(op, ">")) Emit("jg L%d", l);
    else if (!strcmp(op, "<=")) { Emit("jl L%d", l); Emit("je L%d", l); }
    else { Emit("jg L%d", l); Emit("je L%d", l); }
    Emit("mov a, 0"); Place(l);
  }
}
void ParseEq() {
  ParseCmp();
  while (!failed && (IsOp("==") || IsOp("!="))) {
    bool eq = IsOp("==");
    Next(); Binary(ParseCmp);
    int l = NewLabel();
    Emit("cmp a, b"); Emit("mov a, 1"); Emit(eq ? "je L%d" : "jne L%d", l);
    Emit("mov a, 0"); Place(l);
  }
}
void ParseAnd() {
  ParseEq();
  while (!failed && IsOp("&&")) {
    Next(); Norm(); Emit("push a"); ParseEq(); Norm();
    Emit("mov b, a"); Emit("pop a"); Emit("mul a, b");
  }
}
void ParseOr() {
  ParseAnd();
  while (!failed && IsOp("||")) {
    Next(); Norm(); Emit("push a"); ParseAnd(); Norm();
    Emit("mov b, a"); Emit("pop a"); Emit("add a, b"); Norm();
  }
}
void ParseExpr() { ParseOr(); }

void ParseStmt();

void ParseBlock() {
  Expect("{");
  while (!failed && !IsOp("}") && tok.type != T_END) ParseStmt();
  Expect("}");
}

void ParseStmt() {
  if (failed) return;
  if (IsOp("{")) { ParseBlock(); return; }
  if (IsOp(";")) { Next(); return; }
  if (tok.type != T_ID) { Error("syntax error"); return; }

  if (IsKw("var")) {
    Next();
    if (tok.type != T_ID) { Error("variable name expected"); return; }
    if (FindVar(tok.text) >= 0) { Error("variable '%s' already declared", tok.text); return; }
    int addr = DeclVar(tok.text); Next();
    if (Accept("=")) { ParseExpr(); } else { Emit("mov a, 0"); }
    Emit("st %d, a", addr); Expect(";"); return;
  }
  if (IsKw("printf")) {
    Next(); Expect("(");
    if (tok.type != T_STR) { Error("printf: format string expected"); return; }
    char fmt[128]; strcpy(fmt, tok.text); Next();
    for (const char* f = fmt; *f && !failed; ++f) {
      if (*f != '%') { Emit("mov c, %d", (unsigned char)*f); Emit("outc c"); continue; }
      ++f;
      if (*f == '%') { Emit("mov c, 37"); Emit("outc c"); continue; }
      if (*f != 'd' && *f != 'c' && *f != 's') {
        Error("printf: unsupported conversion '%%%c'", *f ? *f : ' '); return;
      }
      const char conv = *f;
      Expect(",");
      if (conv == 's') {
        if (tok.type != T_STR) { Error("printf: %%s needs a string literal"); return; }
        for (const char* t = tok.text; *t; ++t) { Emit("mov c, %d", (unsigned char)*t); Emit("outc c"); }
        Next();
      } else {
        ParseExpr(); Emit(conv == 'd' ? "outn a" : "outc a");
      }
    }
    if (IsOp(",")) { Error("printf: too many arguments"); return; }
    Expect(")"); Expect(";"); return;
  }
  if (IsKw("scanf")) { ParseScanf(); Expect(";"); return; }
  if (IsKw("if")) {
    Next(); Expect("("); ParseExpr(); Expect(")");
    int lelse = NewLabel(), lend = NewLabel();
    Emit("cmp a, 0"); Emit("je L%d", lelse);
    ParseBlock();
    if (IsKw("else")) {
      Next(); Emit("jmp L%d", lend); Place(lelse);
      if (IsKw("if")) ParseStmt(); else ParseBlock();
      Place(lend);
    } else {
      Place(lelse);
    }
    return;
  }
  if (IsKw("while")) {
    Next(); int ltop = NewLabel(), lend = NewLabel();
    Place(ltop); Expect("("); ParseExpr(); Expect(")");
    Emit("cmp a, 0"); Emit("je L%d", lend);
    ParseBlock(); Emit("jmp L%d", ltop); Place(lend); return;
  }
  if (IsKw("func")) {
    Next();
    if (tok.type != T_ID) { Error("function name expected"); return; }
    char fname[64]; strcpy(fname, tok.text); Next();
    Expect("("); Expect(")");
    int lskip = NewLabel();
    Emit("jmp L%d", lskip); Emit("F_%s:", fname);
    ParseBlock();
    Emit("ret"); Place(lskip); return;
  }
  if (IsKw("return")) { Next(); Emit("ret"); Expect(";"); return; }
  if (IsKw("halt")) { Next(); Emit("halt"); Expect(";"); return; }

  // 代入 または 関数呼び出し
  char name[64]; strcpy(name, tok.text); Next();
  if (Accept("=")) {
    int addr = FindVar(name);
    if (addr < 0) { Error("undefined variable '%s'", name); return; }
    ParseExpr(); Emit("st %d, a", addr); Expect(";"); return;
  }
  if (Accept("(")) {
    Expect(")");
    if (!FindFunc(name)) { Error("undefined function '%s'", name); return; }
    Emit("call F_%s", name); Expect(";"); return;
  }
  Error("syntax error near '%s'", name);
}


// func 名だけ先に収集する(前方参照を許すため)。
void ScanFuncs() {
  const char* save = p; int save_line = cur_line; Tok save_tok = tok;
  Next();
  while (tok.type != T_END && !failed) {
    if (IsKw("func")) {
      Next();
      if (tok.type == T_ID && nfuncs < 256) strncpy(funcs[nfuncs++].name, tok.text, 63);
    }
    Next();
  }
  p = save; cur_line = save_line; tok = save_tok; failed = false;
}

void ParseProgram() {
  ScanFuncs();
  Next();
  while (!failed && tok.type != T_END) ParseStmt();
  Emit("halt");
}

}  // namespace

int main(int argc, char** argv) {
  const char* path = nullptr;
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "-S") == 0) dump_only = true; else path = argv[i];
  }
  if (!path) { printf("usage: xhl [-S] <file.xh>\n"); return 1; }
  FILE* fp = fopen(path, "r");
  if (!fp) { printf("failed to open: %s\n", path); return 1; }
  size_t n = fread(src, 1, sizeof src - 1, fp);
  src[n] = 0; fclose(fp);

  xvm::Reset();
  p = src;
  ParseProgram();
  if (failed) return 1;
  if (dump_only) return 0;
  if (!xvm::Link()) return 1;
  return xvm::Run();
}
