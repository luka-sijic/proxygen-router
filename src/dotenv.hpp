#pragma once
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unordered_map>

namespace dotenv {

// trim helpers
static inline void ltrim(std::string &s) {
  size_t i = 0;
  while (i < s.size() && std::isspace((unsigned char)s[i]))
    ++i;
  s.erase(0, i);
}
static inline void rtrim(std::string &s) {
  size_t i = s.size();
  while (i > 0 && std::isspace((unsigned char)s[i - 1]))
    --i;
  s.resize(i);
}
static inline void trim(std::string &s) {
  rtrim(s);
  ltrim(s);
}

static inline bool is_name_char(char c, bool first) {
  return (c == '_' || std::isalpha((unsigned char)c) ||
          (!first && std::isdigit((unsigned char)c)));
}

// Very small ${VAR} expander from current process env (no nesting/recursion)
static inline std::string expand_env_refs(const std::string &in) {
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] == '$' && i + 1 < in.size() && in[i + 1] == '{') {
      size_t j = i + 2; // after ${
      std::string name;
      if (j < in.size() && (is_name_char(in[j], true))) {
        name.push_back(in[j++]);
        while (j < in.size() && is_name_char(in[j], false))
          name.push_back(in[j++]);
      }
      if (j < in.size() && in[j] == '}') {
        const char *v = std::getenv(name.c_str());
        if (v)
          out.append(v);
        i = j; // jump to }
        continue;
      }
    }
    out.push_back(in[i]);
  }
  return out;
}

static inline bool set_env(const std::string &key, const std::string &val,
                           bool overwrite) {
#ifdef _WIN32
  if (!overwrite) {
    size_t dummy;
    if (getenv(key.c_str()))
      return true;
  }
  return _putenv_s(key.c_str(), val.c_str()) == 0;
#else
  return setenv(key.c_str(), val.c_str(), overwrite ? 1 : 0) == 0;
#endif
}

// Parse a single line KEY=VALUE (supports `export KEY=...`, quotes, \ escapes,
// inline # comments when unquoted)
static inline bool parse_line(const std::string &line_in, std::string &key_out,
                              std::string &val_out) {
  std::string s = line_in;
  trim(s);
  if (s.empty() || s[0] == '#')
    return false;

  // optional "export "
  if (s.rfind("export ", 0) == 0) {
    s.erase(0, 7);
    trim(s);
  }

  // split on first '='
  size_t eq = s.find('=');
  if (eq == std::string::npos)
    return false;

  std::string key = s.substr(0, eq);
  std::string val = s.substr(eq + 1);

  rtrim(key);
  ltrim(val);

  // validate key
  if (key.empty() || !is_name_char(key[0], true))
    return false;
  for (size_t i = 1; i < key.size(); ++i)
    if (!is_name_char(key[i], false))
      return false;

  // value parsing
  std::string out;
  if (!val.empty() && (val[0] == '"' || val[0] == '\'')) {
    char q = val[0];
    bool esc = false;
    for (size_t i = 1; i < val.size(); ++i) {
      char c = val[i];
      if (esc) {
        out.push_back(c);
        esc = false;
        continue;
      }
      if (c == '\\') {
        esc = true;
        continue;
      }
      if (c == q) { // ignore anything after closing quote except whitespace and
                    // comment
        std::string tail = val.substr(i + 1);
        trim(tail);
        if (!tail.empty() && tail[0] == '#') { /* comment */
        }
        break;
      }
      out.push_back(c);
    }
  } else {
    // unquoted: read to end, strip trailing inline comment (#...) if preceded
    // by space
    size_t hash = std::string::npos;
    for (size_t i = 0; i < val.size(); ++i) {
      if (val[i] == '#') {
        hash = i;
        break;
      }
    }
    std::string raw = (hash == std::string::npos) ? val : val.substr(0, hash);
    trim(raw);
    out = raw;
  }

  key_out = key;
  val_out = out;
  return true;
}

// Load .env into process env; does not overwrite existing vars by default
// Returns true if file was read (even if some lines invalid); false if file
// missing/unreadable.
static inline bool load(const std::string &path = ".env",
                        bool overwrite = false, bool expand = true) {
  std::ifstream in(path);
  if (!in)
    return false;

  std::string line, key, val;
  while (std::getline(in, line)) {
    if (!parse_line(line, key, val))
      continue;
    if (expand)
      val = expand_env_refs(val);
    set_env(key, val, overwrite);
  }
  return true;
}

} // namespace dotenv
