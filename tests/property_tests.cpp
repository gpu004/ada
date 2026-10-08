// Property-based tests driven by Hegel (https://github.com/hegeldev/hegel-cpp).
//
// Every input handed to ada is valid UTF-8, as required by the public API.
// Set ADA_HEGEL_TEST_CASES to change the number of cases per property.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ada.h"
#include "ada/url_search_params.h"

#include <hegel/gtest.h>

namespace gs = hegel::generators;

namespace {

hegel::Settings settings() {
  hegel::Settings s;
  s.test_cases = 2000;
  if (const char* env = std::getenv("ADA_HEGEL_TEST_CASES")) {
    s.test_cases = std::strtoull(env, nullptr, 10);
  }
  s.suppress_health_check = {hegel::HealthCheck::TooSlow,
                             hegel::HealthCheck::FilterTooMuch};
  return s;
}

const std::string url_alphabet =
    "abcdefxyzABCXYZ0123456789-._~!$&'()*+,;=:@/?#[]%\\ \t\n|^`{}<>\"";

gs::Generator<std::string> interesting_fragments() {
  return gs::sampled_from<std::string>({
      "",
      "%",
      "%2e",
      "%2E",
      ".",
      "..",
      "%2e%2e",
      ".%2E",
      "%00",
      "%41",
      "%zz",
      "%%",
      "\\",
      "/",
      "//",
      "@",
      ":",
      "[",
      "]",
      "#",
      "?",
      " ",
      "\t",
      "xn--",
      "xn--a",
      "xn--nxasmq6b",
      "\xc3\x9f",          // U+00DF
      "\xc3\xa9",          // U+00E9
      "\xe3\x80\x82",      // U+3002 ideographic full stop
      "\xef\xbc\x8e",      // U+FF0E fullwidth full stop
      "\xef\xbc\x91",      // U+FF11 fullwidth digit one
      "\xe2\x80\x8d",      // U+200D zero width joiner
      "\xc2\xad",          // U+00AD soft hyphen
      "\xf0\x9f\x92\xa9",  // U+1F4A9
      "\xef\xbf\xbd",      // U+FFFD
      "%EF%BC%91",
      "0x7f",
      "0x",
      "0",
      "09",
      "1.2.3.4",
      "1.2.3.4.",
      "255.255.255.255",
      "4294967295",
      "4294967296",
      "0xffffffff",
      "::1",
      "[::1]",
      "[::ffff:1.2.3.4]",
      "[1:2:3:4:5:6:7:8]",
      "[::]",
      "localhost",
      "LOCALHOST",
      "C:",
      "c|",
      "%3A",
      "%2F",
      "%5C",
      "%40",
  });
}

gs::Generator<std::string> chunk() {
  return gs::one_of<std::string>({
      interesting_fragments(),
      gs::text({.max_size = 10, .alphabet = url_alphabet}),
      gs::text({.max_size = 4}),
  });
}

gs::Generator<std::string> chunks(size_t max) {
  return gs::vectors(chunk(), {.max_size = max})
      .map([](const std::vector<std::string>& parts) {
        std::string out;
        for (const auto& p : parts) out += p;
        return out;
      });
}

gs::Generator<std::string> schemes() {
  return gs::one_of<std::string>({
      gs::sampled_from<std::string>({"http", "https", "ws", "wss", "ftp",
                                     "file", "HTTP", "FiLe", "javascript",
                                     "data", "blob", "mailto", "web+demo", "a",
                                     "sc", "non-spec", "git+ssh"}),
      gs::text({.min_size = 1, .max_size = 6, .alphabet = "abcz09+-."}),
  });
}

gs::Generator<std::string> hosts() {
  return gs::one_of<std::string>({
      gs::sampled_from<std::string>(
          {"example.com", "EXAMPLE.com", "a.b.c", "localhost", "127.0.0.1",
           "0x7f.1", "0177.0.0.1", "1.2.3", "1.2.3.4.5", "[::1]",
           "[0:0:0:0:0:ffff:7f00:1]", "[::127.0.0.1]", "xn--nxasmq6b.com",
           "\xc3\x9f.de", "fa\xc3\x9f.ExAmPlE", ""}),
      gs::vectors(chunk(), {.max_size = 4})
          .map([](const std::vector<std::string>& labels) {
            std::string out;
            for (size_t i = 0; i < labels.size(); i++) {
              if (i) out += '.';
              out += labels[i];
            }
            return out;
          }),
  });
}

// Structured URL-like strings that are more likely to parse than raw text.
gs::Generator<std::string> structured_urls() {
  return gs::compose([](const hegel::TestCase& tc) {
    std::string out = tc.draw(schemes());
    out += tc.draw(gs::sampled_from<std::string>(
        {":", "://", ":/", ":\\\\", ":///", "://///", ":/\\"}));
    if (tc.draw(gs::booleans())) {
      out += tc.draw(chunks(2));
      if (tc.draw(gs::booleans())) {
        out += ':';
        out += tc.draw(chunks(2));
      }
      out += '@';
    }
    out += tc.draw(hosts());
    if (tc.draw(gs::booleans())) {
      out += ':';
      out += tc.draw(gs::one_of<std::string>(
          {gs::integers<uint32_t>({.max_value = 70000}).map([](uint32_t v) {
             return std::to_string(v);
           }),
           chunks(1)}));
    }
    auto segments = tc.draw(gs::vectors(chunk(), {.max_size = 5}));
    for (const auto& s : segments) {
      out += tc.draw(gs::sampled_from<std::string>({"/", "\\"}));
      out += s;
    }
    if (tc.draw(gs::booleans())) {
      out += '?';
      out += tc.draw(chunks(3));
    }
    if (tc.draw(gs::booleans())) {
      out += '#';
      out += tc.draw(chunks(3));
    }
    return out;
  });
}

gs::Generator<std::string> url_inputs() {
  return gs::one_of<std::string>({
      structured_urls(),
      gs::compose([](const hegel::TestCase& tc) {
        return tc.draw(schemes()) + ":" + tc.draw(chunks(6));
      }),
      chunks(8),
  });
}

gs::Generator<std::string> bases() {
  return gs::one_of<std::string>({
      gs::sampled_from<std::string>(
          {"http://example.com/a/b/c?q#f", "https://u:p@h:8080/x/",
           "file:///C:/dir/file", "file://host/share/", "non-spec:/a/b",
           "non-spec://h/p", "data:text/plain,hi", "blob:https://x/y",
           "about:blank", "web+demo:/.//not-a-host/", "ws://[::1]/"}),
      structured_urls(),
  });
}

enum class component {
  href,
  protocol,
  username,
  password,
  host,
  hostname,
  port,
  pathname,
  search,
  hash
};

constexpr component all_components[] = {
    component::href,     component::protocol, component::username,
    component::password, component::host,     component::hostname,
    component::port,     component::pathname, component::search,
    component::hash};

const char* component_name(component c) {
  switch (c) {
    case component::href:
      return "href";
    case component::protocol:
      return "protocol";
    case component::username:
      return "username";
    case component::password:
      return "password";
    case component::host:
      return "host";
    case component::hostname:
      return "hostname";
    case component::port:
      return "port";
    case component::pathname:
      return "pathname";
    case component::search:
      return "search";
    case component::hash:
      return "hash";
  }
  return "?";
}

template <class T>
std::string get(const T& u, component c) {
  switch (c) {
    case component::href:
      return std::string(u.get_href());
    case component::protocol:
      return std::string(u.get_protocol());
    case component::username:
      return std::string(u.get_username());
    case component::password:
      return std::string(u.get_password());
    case component::host:
      return std::string(u.get_host());
    case component::hostname:
      return std::string(u.get_hostname());
    case component::port:
      return std::string(u.get_port());
    case component::pathname:
      return std::string(u.get_pathname());
    case component::search:
      return std::string(u.get_search());
    case component::hash:
      return std::string(u.get_hash());
  }
  return {};
}

// Returns whether the setter reported success (search/hash always succeed).
template <class T>
bool set(T& u, component c, std::string_view v) {
  switch (c) {
    case component::href:
      return u.set_href(v);
    case component::protocol:
      return u.set_protocol(v);
    case component::username:
      return u.set_username(v);
    case component::password:
      return u.set_password(v);
    case component::host:
      return u.set_host(v);
    case component::hostname:
      return u.set_hostname(v);
    case component::port:
      return u.set_port(v);
    case component::pathname:
      return u.set_pathname(v);
    case component::search:
      u.set_search(v);
      return true;
    case component::hash:
      u.set_hash(v);
      return true;
  }
  return false;
}

gs::Generator<component> components() {
  return gs::sampled_from<component>(std::vector<component>(
      std::begin(all_components), std::end(all_components)));
}

gs::Generator<std::string> setter_values(component c) {
  switch (c) {
    case component::href:
      return url_inputs();
    case component::protocol:
      return gs::one_of<std::string>(
          {schemes(), schemes().map([](auto s) { return s + ":"; }),
           chunks(2)});
    case component::host:
    case component::hostname:
      return gs::one_of<std::string>(
          {hosts(), gs::compose([](const hegel::TestCase& tc) {
             return tc.draw(hosts()) + ":" + tc.draw(chunks(1));
           })});
    case component::port:
      return gs::one_of<std::string>(
          {gs::integers<uint32_t>({.max_value = 70000}).map([](uint32_t v) {
             return std::to_string(v);
           }),
           chunks(2)});
    default:
      return chunks(4);
  }
}

template <class T>
void expect_same_components(const T& a, const ada::url& b) {
  for (component c : all_components) {
    EXPECT_EQ(get(a, c), get(b, c)) << "component " << component_name(c);
  }
}

bool is_ascii(std::string_view s) {
  return std::all_of(s.begin(), s.end(), [](char c) {
    return static_cast<unsigned char>(c) < 0x80;
  });
}

bool has_ace_label(std::string_view domain) {
  size_t start = 0;
  while (start <= domain.size()) {
    size_t end = std::min(domain.find('.', start), domain.size());
    std::string_view label = domain.substr(start, end - start);
    if (label.size() >= 4 && (label[0] | 0x20) == 'x' &&
        (label[1] | 0x20) == 'n' && label[2] == '-' && label[3] == '-') {
      return true;
    }
    start = end + 1;
  }
  return false;
}

bool is_ascii_without_c0_or_space_controls(std::string_view s) {
  return std::all_of(s.begin(), s.end(), [](char ch) {
    auto c = static_cast<unsigned char>(ch);
    return c > 0x1f && c < 0x7f;
  });
}

void check_aggregator_offsets(const ada::url_aggregator& u) {
  ASSERT_TRUE(u.validate()) << u.to_diagram();
  const auto& c = u.get_components();
  std::string_view href = u.get_href();
  ASSERT_EQ(u.get_href_size(), href.size());
  ASSERT_LE(c.protocol_end, href.size());
  EXPECT_EQ(href.substr(0, c.protocol_end), u.get_protocol());
  ASSERT_LE(c.pathname_start, href.size());
  size_t path_end = std::min<size_t>(
      {href.size(), size_t(c.search_start), size_t(c.hash_start)});
  EXPECT_EQ(href.substr(c.pathname_start, path_end - c.pathname_start),
            u.get_pathname());
  if (c.search_start != ada::url_components::omitted) {
    size_t end = std::min<size_t>(href.size(), c.hash_start);
    std::string_view search = href.substr(c.search_start, end - c.search_start);
    // get_search() hides a lone "?".
    EXPECT_EQ(search == "?" ? std::string_view() : search, u.get_search());
  } else {
    EXPECT_EQ(u.get_search(), "");
  }
  if (c.hash_start != ada::url_components::omitted) {
    std::string_view hash = href.substr(c.hash_start);
    EXPECT_EQ(hash == "#" ? std::string_view() : hash, u.get_hash());
  } else {
    EXPECT_EQ(u.get_hash(), "");
  }
  if (c.port != ada::url_components::omitted) {
    EXPECT_EQ(std::to_string(c.port), u.get_port());
  } else {
    EXPECT_EQ(u.get_port(), "");
  }
}

// Reconstructs href from the getters, following the URL serializer.
template <class T>
std::string serialize_from_getters(const T& u) {
  std::string out(u.get_protocol());
  if (u.has_hostname()) {
    out += "//";
    if (!u.get_username().empty() || !u.get_password().empty()) {
      out += u.get_username();
      if (!u.get_password().empty()) {
        out += ':';
        out += u.get_password();
      }
      out += '@';
    }
    out += u.get_host();
  } else if (u.get_pathname().size() > 1 && u.get_pathname()[0] == '/' &&
             u.get_pathname()[1] == '/') {
    out += "/.";
  }
  out += u.get_pathname();
  out += u.get_search();
  if (u.get_search().empty() && u.has_search()) out += '?';
  out += u.get_hash();
  if (u.get_hash().empty() && u.has_hash()) out += '#';
  return out;
}

// UTF-16 code unit ordering, as required by URLSearchParams.sort().
std::u16string to_utf16(std::string_view s) {
  std::u16string out;
  size_t i = 0;
  while (i < s.size()) {
    auto c = static_cast<unsigned char>(s[i]);
    uint32_t cp;
    size_t len;
    if (c < 0x80) {
      cp = c;
      len = 1;
    } else if ((c & 0xe0) == 0xc0) {
      cp = c & 0x1f;
      len = 2;
    } else if ((c & 0xf0) == 0xe0) {
      cp = c & 0x0f;
      len = 3;
    } else {
      cp = c & 0x07;
      len = 4;
    }
    for (size_t k = 1; k < len; k++) {
      cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3f);
    }
    i += len;
    if (cp >= 0x10000) {
      cp -= 0x10000;
      out += char16_t(0xd800 + (cp >> 10));
      out += char16_t(0xdc00 + (cp & 0x3ff));
    } else {
      out += char16_t(cp);
    }
  }
  return out;
}

using pairs = std::vector<std::pair<std::string, std::string>>;

pairs entries(const ada::url_search_params& p) {
  pairs out;
  for (const auto& kv : p) out.emplace_back(kv.first, kv.second);
  return out;
}

gs::Generator<std::string> form_strings() {
  return gs::one_of<std::string>(
      {gs::text({.max_size = 8}),
       gs::text({.max_size = 8, .alphabet = "ab=&+% %2B%3D\t\n#?"}),
       interesting_fragments()});
}

}  // namespace

// Parsing the serialization of a parsed URL must give back the same URL.
TEST(ParseProperties, HrefRoundTripsUrlAggregator) {
  hegel::test(
      [](hegel::TestCase& tc) {
        auto input = tc.draw("input", url_inputs());
        auto u = ada::parse<ada::url_aggregator>(input);
        if (!u) return;
        std::string href(u->get_href());
        auto again = ada::parse<ada::url_aggregator>(href);
        ASSERT_TRUE(again) << "href does not reparse: " << href;
        ASSERT_EQ(again->get_href(), href);
      },
      settings());
}

TEST(ParseProperties, HrefRoundTripsUrl) {
  hegel::test(
      [](hegel::TestCase& tc) {
        auto input = tc.draw("input", url_inputs());
        auto u = ada::parse<ada::url>(input);
        if (!u) return;
        std::string href = u->get_href();
        auto again = ada::parse<ada::url>(href);
        ASSERT_TRUE(again) << "href does not reparse: " << href;
        ASSERT_EQ(again->get_href(), href);
      },
      settings());
}

// ada::url and ada::url_aggregator are two implementations of the same
// standard and must agree on everything observable.
TEST(ParseProperties, UrlAndAggregatorAgree) {
  hegel::test(
      [](hegel::TestCase& tc) {
        auto input = tc.draw("input", url_inputs());
        auto a = ada::parse<ada::url_aggregator>(input);
        auto b = ada::parse<ada::url>(input);
        ASSERT_EQ(bool(a), bool(b));
        if (!a) return;
        expect_same_components(*a, *b);
        EXPECT_EQ(a->get_origin(), b->get_origin());
        EXPECT_EQ(a->has_port(), b->has_port());
        EXPECT_EQ(a->has_hash(), b->has_hash());
        EXPECT_EQ(a->has_search(), b->has_search());
        EXPECT_EQ(a->has_credentials(), b->has_credentials());
        EXPECT_EQ(a->has_hostname(), b->has_hostname());
        EXPECT_EQ(a->has_empty_hostname(), b->has_empty_hostname());
        EXPECT_EQ(a->has_valid_domain(), b->has_valid_domain());
        EXPECT_EQ(a->get_pathname_length(), b->get_pathname_length());
      },
      settings());
}

TEST(ParseProperties, UrlAndAggregatorAgreeWithBase) {
  hegel::test(
      [](hegel::TestCase& tc) {
        auto base_input = tc.draw("base", bases());
        auto base_a = ada::parse<ada::url_aggregator>(base_input);
        auto base_b = ada::parse<ada::url>(base_input);
        ASSERT_EQ(bool(base_a), bool(base_b));
        tc.assume(bool(base_a));
        auto input = tc.draw("input", url_inputs());
        auto a = ada::parse<ada::url_aggregator>(input, &*base_a);
        auto b = ada::parse<ada::url>(input, &*base_b);
        ASSERT_EQ(bool(a), bool(b));
        if (!a) return;
        expect_same_components(*a, *b);
        check_aggregator_offsets(*a);
        // The result is absolute, so it must reparse on its own.
        auto again = ada::parse<ada::url_aggregator>(a->get_href());
        ASSERT_TRUE(again) << a->get_href();
        EXPECT_EQ(again->get_href(), a->get_href());
      },
      settings());
}

TEST(ParseProperties, CanParseAgreesWithParse) {
  hegel::test(
      [](hegel::TestCase& tc) {
        auto input = tc.draw("input", url_inputs());
        bool with_base = tc.draw("with_base", gs::booleans());
        if (with_base) {
          auto base_input = tc.draw("base", bases());
          std::string_view base_view(base_input);
          auto base = ada::parse<ada::url_aggregator>(base_input);
          bool parsed =
              base &&
              ada::parse<ada::url_aggregator>(input, &*base).has_value();
          ASSERT_EQ(ada::can_parse(input, &base_view), parsed);
        } else {
          ASSERT_EQ(ada::can_parse(input),
                    ada::parse<ada::url_aggregator>(input).has_value());
        }
      },
      settings());
}

// Serialized URLs are pure printable ASCII except for spaces, which the
// standard allows to remain in opaque paths, queries and fragments.
TEST(ParseProperties, HrefIsPrintableAscii) {
  hegel::test(
      [](hegel::TestCase& tc) {
        auto input = tc.draw("input", url_inputs());
        auto u = ada::parse<ada::url_aggregator>(input);
        if (!u) return;
        ASSERT_TRUE(is_ascii_without_c0_or_space_controls(u->get_href()))
            << u->get_href();
      },
      settings());
}

TEST(ParseProperties, ComponentsAreConsistentWithHref) {
  hegel::test(
      [](hegel::TestCase& tc) {
        auto input = tc.draw("input", url_inputs());
        auto u = ada::parse<ada::url_aggregator>(input);
        if (!u) return;
        check_aggregator_offsets(*u);
        EXPECT_EQ(serialize_from_getters(*u), u->get_href());
        auto b = ada::parse<ada::url>(input);
        ASSERT_TRUE(b);
        EXPECT_EQ(serialize_from_getters(*b), b->get_href());
      },
      settings());
}

// Run the same sequence of setter calls on both implementations.
TEST(SetterProperties, SetterSequencesAgree) {
  hegel::test(
      [](hegel::TestCase& tc) {
        auto input = tc.draw("input", url_inputs());
        auto a = ada::parse<ada::url_aggregator>(input);
        auto b = ada::parse<ada::url>(input);
        ASSERT_EQ(bool(a), bool(b));
        tc.assume(bool(a));
        auto steps = tc.draw(
            "steps", gs::integers<int>({.min_value = 1, .max_value = 6}));
        for (int i = 0; i < steps; i++) {
          auto c = tc.draw(components());
          auto value = tc.draw(setter_values(c));
          tc.note(std::string("set_") + component_name(c) + "(\"" + value +
                  "\") on " + std::string(a->get_href()));
          bool ra = set(*a, c, value);
          bool rb = set(*b, c, value);
          ASSERT_EQ(ra, rb) << "setter " << component_name(c);
          ASSERT_TRUE(a->validate()) << a->to_diagram();
          ASSERT_EQ(a->get_href(), b->get_href())
              << "after set_" << component_name(c);
          expect_same_components(*a, *b);
          check_aggregator_offsets(*a);
        }
      },
      settings());
}

// Whatever state the setters leave a URL in, its href must still describe
// that same URL.
TEST(SetterProperties, HrefRoundTripsAfterSetters) {
  hegel::test(
      [](hegel::TestCase& tc) {
        auto input = tc.draw("input", url_inputs());
        auto a = ada::parse<ada::url_aggregator>(input);
        tc.assume(bool(a));
        auto steps = tc.draw(
            "steps", gs::integers<int>({.min_value = 1, .max_value = 4}));
        for (int i = 0; i < steps; i++) {
          auto c = tc.draw(components());
          auto value = tc.draw(setter_values(c));
          tc.note(std::string("set_") + component_name(c) + "(\"" + value +
                  "\") on " + std::string(a->get_href()));
          (void)set(*a, c, value);
        }
        std::string href(a->get_href());
        auto again = ada::parse<ada::url_aggregator>(href);
        ASSERT_TRUE(again) << "href does not reparse: " << href;
        ASSERT_EQ(again->get_href(), href);
        for (component c : all_components) {
          EXPECT_EQ(get(*again, c), get(*a, c))
              << "component " << component_name(c);
        }
      },
      settings());
}

// Setting a component to the value its getter reports must not change it.
TEST(SetterProperties, SettingCurrentValueIsNoOp) {
  hegel::test(
      [](hegel::TestCase& tc) {
        auto input = tc.draw("input", url_inputs());
        auto a = ada::parse<ada::url_aggregator>(input);
        tc.assume(bool(a));
        auto c = tc.draw("component", components());
        std::string before(a->get_href());
        std::string value = get(*a, c);
        // The getters report "" both for absent and for empty search, hash and
        // host, and setting "" makes them empty rather than absent.
        tc.assume(!value.empty() ||
                  (c != component::search && c != component::hash &&
                   c != component::host && c != component::hostname));
        (void)set(*a, c, value);
        ASSERT_EQ(a->get_href(), before)
            << "set_" << component_name(c) << "(\"" << value << "\")";
      },
      settings());
}

// A successful setter call followed by the same call again is idempotent.
TEST(SetterProperties, SettersAreIdempotent) {
  hegel::test(
      [](hegel::TestCase& tc) {
        auto input = tc.draw("input", url_inputs());
        auto a = ada::parse<ada::url_aggregator>(input);
        tc.assume(bool(a));
        auto c = tc.draw("component", components());
        tc.assume(c != component::href && c != component::protocol);
        auto value = tc.draw("value", setter_values(c));
        (void)set(*a, c, value);
        std::string once(a->get_href());
        (void)set(*a, c, value);
        ASSERT_EQ(a->get_href(), once) << "set_" << component_name(c);
      },
      settings());
}

// Pairs appended to URLSearchParams survive serialization and reparsing.
TEST(SearchParamsProperties, AppendSerializeParseRoundTrips) {
  hegel::test(
      [](hegel::TestCase& tc) {
        auto list = tc.draw(
            "pairs", gs::vectors(gs::tuples(form_strings(), form_strings()),
                                 {.max_size = 6}));
        ada::url_search_params params;
        pairs expected;
        for (const auto& [k, v] : list) {
          params.append(k, v);
          expected.emplace_back(k, v);
        }
        std::string serialized = params.to_string();
        ada::url_search_params reparsed(serialized);
        ASSERT_EQ(entries(reparsed), expected) << serialized;
        ASSERT_EQ(reparsed.to_string(), serialized);
      },
      settings());
}

TEST(SearchParamsProperties, ParseSerializeIsIdempotent) {
  hegel::test(
      [](hegel::TestCase& tc) {
        auto input = tc.draw("input", chunks(8));
        ada::url_search_params p(input);
        std::string once = p.to_string();
        ada::url_search_params q(once);
        ASSERT_EQ(q.to_string(), once);
        ASSERT_EQ(entries(q), entries(p));
      },
      settings());
}

// sort() must be a stable sort by UTF-16 code units of the name.
TEST(SearchParamsProperties, SortIsStableByUtf16Name) {
  hegel::test(
      [](hegel::TestCase& tc) {
        auto list =
            tc.draw("pairs", gs::vectors(gs::tuples(gs::text({.max_size = 3}),
                                                    gs::text({.max_size = 2})),
                                         {.max_size = 8}));
        ada::url_search_params params;
        pairs expected;
        for (const auto& [k, v] : list) {
          params.append(k, v);
          expected.emplace_back(k, v);
        }
        std::stable_sort(expected.begin(), expected.end(),
                         [](const auto& x, const auto& y) {
                           return to_utf16(x.first) < to_utf16(y.first);
                         });
        params.sort();
        ASSERT_EQ(entries(params), expected);
      },
      settings());
}

TEST(SearchParamsProperties, SetLeavesSingleValue) {
  hegel::test(
      [](hegel::TestCase& tc) {
        auto input = tc.draw("input", chunks(6));
        auto key = tc.draw("key", form_strings());
        auto value = tc.draw("value", form_strings());
        ada::url_search_params p(input);
        p.set(key, value);
        ASSERT_EQ(p.get_all(key), std::vector<std::string>{value});
        ASSERT_TRUE(p.has(key, value));
        p.remove(key);
        ASSERT_FALSE(p.has(key));
      },
      settings());
}

// Mutating a URL's query through url_search_params and writing it back must
// be stable: the written query parses to the same params.
TEST(SearchParamsProperties, UrlSearchRoundTrip) {
  hegel::test(
      [](hegel::TestCase& tc) {
        auto input = tc.draw("input", url_inputs());
        auto u = ada::parse<ada::url_aggregator>(input);
        tc.assume(bool(u));
        ada::url_search_params p(u->get_search());
        u->set_search(p.to_string());
        ada::url_search_params q(u->get_search());
        ASSERT_EQ(entries(q), entries(p)) << u->get_href();
      },
      settings());
}

TEST(IdnaProperties, ToAsciiIsIdempotent) {
  hegel::test(
      [](hegel::TestCase& tc) {
        auto input = tc.draw("input", hosts());
        std::string ascii = ada::idna::to_ascii(input);
        tc.assume(!ascii.empty());
        ASSERT_EQ(ada::idna::to_ascii(ascii), ascii);
      },
      settings());
}

TEST(IdnaProperties, ToUnicodeThenToAsciiRoundTrips) {
  hegel::test(
      [](hegel::TestCase& tc) {
        auto input = tc.draw("input", hosts());
        std::string ascii = ada::idna::to_ascii(input);
        tc.assume(!ascii.empty());
        std::string unicode = ada::idna::to_unicode(ascii);
        // to_unicode keeps an ACE label it cannot decode. Next to a decoded
        // label that makes the domain non-ASCII, where the URL Standard's
        // ASCII carve-out no longer applies and strict ToASCII rejects it.
        tc.assume(is_ascii(unicode) || !has_ace_label(unicode));
        ASSERT_EQ(ada::idna::to_ascii(unicode), ascii) << unicode;
      },
      settings());
}

// Without ACE labels, labels are processed independently (Bidi aside, which a
// Latin label cannot trigger), so appending a non-ASCII label must not change
// how the others are handled. All-ASCII and non-ASCII inputs take different
// code paths. ACE labels are excluded because the URL Standard accepts invalid
// ones only when the whole domain is ASCII.
TEST(IdnaProperties, AppendingNonAsciiLabelIsIndependent) {
  hegel::test(
      [](hegel::TestCase& tc) {
        auto host = tc.draw(
            "host", gs::one_of<std::string>(
                        {hosts(), gs::text({.min_size = 1,
                                            .max_size = 12,
                                            .alphabet = "abcxn-.019"})}));
        tc.assume(is_ascii(host) && !has_ace_label(host));
        std::string ascii = ada::idna::to_ascii(host);
        tc.assume(!ascii.empty() && ascii.back() != '.');
        // U+00E9 encodes as xn--9ca.
        std::string extended = ada::idna::to_ascii(host + ".\xc3\xa9");
        ASSERT_EQ(extended, ascii + ".xn--9ca");
        // The converse fails legitimately: "1.2.3.4.5" is an invalid IPv4
        // address but "1.2.3.4.5.\xc3\xa9" is a domain.
        // Restricted to domain characters, so that the appended label stays in
        // the host instead of landing in a port, path or IPv6 literal.
        bool plain = std::all_of(host.begin(), host.end(), [](char c) {
          return std::isalnum(static_cast<unsigned char>(c)) || c == '-' ||
                 c == '.';
        });
        if (plain &&
            ada::parse<ada::url_aggregator>("https://" + host + "/")) {
          ASSERT_TRUE(ada::parse<ada::url_aggregator>("https://" + host +
                                                      ".\xc3\xa9/"));
        }
      },
      settings());
}

// The URL host parser and ada::idna must agree for special URLs with a
// plain domain.
TEST(IdnaProperties, HostnameIsIdempotentUnderReparse) {
  hegel::test(
      [](hegel::TestCase& tc) {
        auto host = tc.draw("host", hosts());
        auto u = ada::parse<ada::url_aggregator>("https://" + host + "/");
        tc.assume(bool(u));
        std::string hostname(u->get_hostname());
        auto again =
            ada::parse<ada::url_aggregator>("https://" + hostname + "/");
        ASSERT_TRUE(again) << hostname;
        ASSERT_EQ(again->get_hostname(), hostname);
        ASSERT_TRUE(u->set_hostname(hostname));
        ASSERT_EQ(u->get_hostname(), hostname);
      },
      settings());
}

#if ADA_INCLUDE_URL_PATTERN
namespace {

using regex_provider = ada::url_pattern_regex::std_regex_provider;

std::string strip_prefix(std::string_view s, char c) {
  if (!s.empty() && s.front() == c) s.remove_prefix(1);
  return std::string(s);
}

std::string strip_suffix(std::string_view s, char c) {
  if (!s.empty() && s.back() == c) s.remove_suffix(1);
  return std::string(s);
}

gs::Generator<std::string> path_pattern_segments() {
  return gs::one_of<std::string>({
      gs::sampled_from<std::string>(
          {"users", "me",    "a",      "b",         "files",   "",
           ":id",   ":name", "*",      ":id(\\d+)", "(\\d+)",  ":id?",
           ":x+",   ":x*",   "{a}?",   "{:id}",     "x*",      "%41",
           "A",     "a.b",   "{a/b}?", "*.txt",     ":id.json"}),
      gs::text({.min_size = 1, .max_size = 3, .alphabet = "ab1"}),
  });
}

gs::Generator<std::string> path_patterns() {
  return gs::compose([](const hegel::TestCase& tc) {
    // Long routes exceed the trie and capture limits of url_pattern_list's
    // fast path and exercise its sequential fallback.
    auto max = tc.draw(gs::sampled_from<size_t>({4, 20}));
    auto segments =
        tc.draw(gs::vectors(path_pattern_segments(), {.max_size = max}));
    std::string out;
    for (const auto& s : segments) out += "/" + s;
    if (out.empty() || tc.draw(gs::booleans())) out += "/";
    return out;
  });
}

gs::Generator<std::string> path_inputs() {
  return gs::compose([](const hegel::TestCase& tc) {
    auto segments = tc.draw(gs::vectors(
        gs::one_of<std::string>(
            {gs::sampled_from<std::string>({"users", "me", "a", "b", "files",
                                            "", "42", "x.txt", "a.b", "A",
                                            "%41", "1.json", "a.json"}),
             gs::text({.max_size = 3, .alphabet = "ab1."})}),
        {.max_size = 30}));
    std::string out;
    for (const auto& s : segments) out += "/" + s;
    if (out.empty() || tc.draw(gs::booleans())) out += "/";
    return out;
  });
}

std::optional<ada::url_pattern<regex_provider>> pathname_pattern(
    const std::string& pathname) {
  ada::url_pattern_init init{};
  init.protocol = "https";
  init.hostname = "h";
  init.pathname = pathname;
  auto p = ada::parse_url_pattern<regex_provider>(std::move(init));
  if (!p) return std::nullopt;
  return std::move(*p);
}

}  // namespace

// A URL matches the pattern made of its own escaped components.
TEST(UrlPatternProperties, UrlMatchesItsEscapedComponents) {
  hegel::test(
      [](hegel::TestCase& tc) {
        auto input = tc.draw("input", url_inputs());
        auto u = ada::parse<ada::url_aggregator>(input);
        tc.assume(bool(u));
        using ada::url_pattern_helpers::escape_pattern_string;
        ada::url_pattern_init init{};
        init.protocol =
            escape_pattern_string(strip_suffix(u->get_protocol(), ':'));
        init.username = escape_pattern_string(u->get_username());
        init.password = escape_pattern_string(u->get_password());
        // Pattern hostnames are always canonicalized as special-URL hosts, so
        // opaque hosts (which keep their case) cannot be matched literally.
        init.hostname =
            ada::scheme::is_special(strip_suffix(u->get_protocol(), ':'))
                ? escape_pattern_string(u->get_hostname())
                : "*";
        init.port = escape_pattern_string(u->get_port());
        init.pathname = escape_pattern_string(u->get_pathname());
        init.search = escape_pattern_string(strip_prefix(u->get_search(), '?'));
        init.hash = escape_pattern_string(strip_prefix(u->get_hash(), '#'));
        auto pattern = ada::parse_url_pattern<regex_provider>(std::move(init));
        ASSERT_TRUE(pattern) << "pattern rejected for " << u->get_href();
        std::string_view href = u->get_href();
        auto matched = pattern->test(href);
        ASSERT_TRUE(matched) << u->get_href();
        ASSERT_TRUE(*matched) << u->get_href();
        auto exec = pattern->exec(href);
        ASSERT_TRUE(exec && exec->has_value());
        EXPECT_EQ((*exec)->pathname.input, u->get_pathname());
      },
      settings());
}

// url_pattern_list must agree with testing each pathname pattern on its own.
TEST(UrlPatternProperties, ListAgreesWithIndividualPatterns) {
  hegel::test(
      [](hegel::TestCase& tc) {
        auto sources = tc.draw(
            "patterns",
            gs::vectors(path_patterns(), {.min_size = 1, .max_size = 5}));
        std::vector<std::optional<ada::url_pattern<regex_provider>>> single;
        bool all_valid = true;
        for (const auto& s : sources) {
          single.push_back(pathname_pattern(s));
          all_valid = all_valid && single.back().has_value();
        }
        std::vector<std::string_view> views(sources.begin(), sources.end());
        auto list = ada::parse_url_pattern_list<regex_provider>(views);
        ASSERT_EQ(bool(list), all_valid);
        tc.assume(all_valid);

        std::string raw;
        if (tc.draw("instantiate_route", gs::booleans())) {
          // Fill one route's dynamic segments to make a match likely.
          auto route = tc.draw(gs::sampled_from<std::string>(sources));
          size_t start = 1;
          while (start <= route.size()) {
            size_t end = route.find('/', start);
            if (end == std::string::npos) end = route.size();
            std::string seg = route.substr(start, end - start);
            bool dynamic = seg.find_first_of(":*({") != std::string::npos;
            raw += "/";
            raw += dynamic ? tc.draw(gs::sampled_from<std::string>(
                                 {"42", "a", "x.txt", "1.json", "a/b", ""}))
                           : seg;
            start = end + 1;
          }
        } else {
          raw = tc.draw("path", path_inputs());
        }
        auto url = ada::parse<ada::url_aggregator>("https://h" + raw);
        ASSERT_TRUE(url);
        std::string path(url->get_pathname());
        std::string href(url->get_href());
        tc.note("canonical pathname: " + path);

        auto result = list->match(path);
        std::vector<bool> individual;
        for (auto& p : single) {
          std::string_view href_view(href);
          auto r = p->test(href_view);
          ASSERT_TRUE(r);
          individual.push_back(*r);
        }
        bool any = std::find(individual.begin(), individual.end(), true) !=
                   individual.end();
        ASSERT_EQ(result.has_match(), any);
        if (!result.has_match()) return;
        auto idx = static_cast<size_t>(result.route_index);
        ASSERT_LT(idx, sources.size());
        ASSERT_TRUE(individual[idx]) << "route " << sources[idx];

        std::string_view href_view(href);
        auto exec = single[idx]->exec(href_view);
        ASSERT_TRUE(exec && exec->has_value());
        const auto& groups = (*exec)->pathname.groups;
        const auto& names = list->group_names(idx);
        if (result.regexp_route) {
          ASSERT_EQ(result.regexp_groups.size(), names.size());
          for (size_t k = 0; k < names.size(); k++) {
            EXPECT_EQ(result.regexp_groups[k], groups.at(names[k]))
                << "group " << names[k];
          }
        } else if (!result.captures_truncated) {
          ASSERT_EQ(result.capture_count, names.size());
          for (size_t k = 0; k < names.size(); k++) {
            auto cap = result.captures[k];
            EXPECT_EQ(
                std::optional<std::string>(path.substr(cap.offset, cap.length)),
                groups.at(names[k]))
                << "group " << names[k];
          }
        }
      },
      settings());
}
#endif  // ADA_INCLUDE_URL_PATTERN
