////////////////////////////////////////////////////////////////////////////////
//
// Copyright 2015 - 2016, Paul Beckingham, Federico Hernandez.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// http://www.opensource.org/licenses/mit-license.php
//
////////////////////////////////////////////////////////////////////////////////

#include <cmake.h>
#include <Rules.h>
#include <FS.h>
#include <shared.h>
#include <format.h>
#include <sstream>
#include <tuple>
#include <inttypes.h>

static std::map <std::string, std::string> defaultSettings =
{
  {"confirmation", "yes"},
};

////////////////////////////////////////////////////////////////////////////////
// Nested files are supported, with the following construct:
//   import /absolute/path/to/file
void Rules::load (const std::string& file, int nest /* = 1 */)
{
  if (nest > 10)
    throw std::string ("Rules files may only be nested to 10 levels.");

  // First time in, load the default values.
  if (nest == 1)
  {
    _settings = defaultSettings;
    _original_file = File (file)._data;
  }

  // Read the file, then parse the contents.
  std::string contents;
  if (File::read (file, contents) && contents.length ())
    parse (contents, nest);
}

////////////////////////////////////////////////////////////////////////////////
std::string Rules::file () const
{
  return _original_file;
}

////////////////////////////////////////////////////////////////////////////////
bool Rules::has (const std::string& key) const
{
  return _settings.find (key) != _settings.end ();
}

////////////////////////////////////////////////////////////////////////////////
// Return the configuration value given the specified key.
std::string Rules::get (const std::string& key) const
{
  auto found = _settings.find (key);
  if (found != _settings.end ())
    return found->second;

  return "";
}

////////////////////////////////////////////////////////////////////////////////
int Rules::getInteger (const std::string& key) const
{
  auto found = _settings.find (key);
  if (found != _settings.end ())
    return strtoimax (found->second.c_str (), nullptr, 10);

  return 0;
}

////////////////////////////////////////////////////////////////////////////////
double Rules::getReal (const std::string& key) const
{
  auto found = _settings.find (key);
  if (found != _settings.end ())
    return strtod (found->second.c_str (), nullptr);

  return 0.0;
}

////////////////////////////////////////////////////////////////////////////////
bool Rules::getBoolean (const std::string& key) const
{
  auto found = _settings.find (key);
  if (found != _settings.end ())
  {
    auto value = lowerCase (found->second);
    if (value == "true"   ||
        value == "1"      ||
        value == "y"      ||
        value == "yes"    ||
        value == "on")
      return true;
  }

  return false;
}

////////////////////////////////////////////////////////////////////////////////
void Rules::set (const std::string& key, const int value)
{
  _settings[key] = format (value);
}

////////////////////////////////////////////////////////////////////////////////
void Rules::set (const std::string& key, const double value)
{
  _settings[key] = format (value, 1, 8);
}

////////////////////////////////////////////////////////////////////////////////
void Rules::set (const std::string& key, const std::string& value)
{
  _settings[key] = value;
}

////////////////////////////////////////////////////////////////////////////////
// Provide a vector of all configuration keys.
std::vector <std::string> Rules::all () const
{
  std::vector <std::string> items;
  for (const auto& it : _settings)
    items.push_back (it.first);

  return items;
}

////////////////////////////////////////////////////////////////////////////////
std::string Rules::dump () const
{
  std::stringstream out;
  out << "Rules\n"
      << "  _original_file " << _original_file
      << "\n";

  out << "  Settings\n";
  for (const auto& item : _settings)
    out << "    " << item.first << "=" << item.second << "\n";

  return out.str ();
}

////////////////////////////////////////////////////////////////////////////////
void Rules::parse (const std::string& input, int nest /* = 1 */)
{
  bool inRule = false;
  std::string ruleDef;

  // Remove comments from input.
  for (auto& line : split (input, '\n'))
  {
    auto comment = line.find ("#");
    if (comment != std::string::npos)
    {
      line.resize (comment);
      line = rtrim (line);
    }

    // Tokenize the line for convenience, but capture the indentation level.
    // tokens.
    auto indent = line.find_first_not_of (' ');

    auto tokens = Lexer::tokenize (line);
    if (tokens.size ())
    {
      if (inRule)
      {
        if (indent == 0)
        {
          inRule = false;
          parseRule (ruleDef);
          ruleDef = "";
        }
        else
        {
          ruleDef += line + "\n";
        }
      }

      // Note: this should NOT be an 'else' to the above 'if (inRule)', because
      //       there are lines where both blocks need to run.
      if (! inRule)
      {
        auto firstWord = std::get <0> (tokens[0]);

        // Top-level rule definition:
        //   'define'
        //      ...
        //
        if (firstWord == "define")
        {
          if (ruleDef != "")
          {
            parseRule (ruleDef);
            ruleDef = "";
          }

          inRule = true;
          ruleDef = line + "\n";
        }

        // Top-level import:
        //   'import' <file>
        else if (firstWord == "import" &&
                 tokens.size () == 2   &&
                 std::get <1> (tokens[1]) == Lexer::Type::path)
        {
          File imported (std::get <0> (tokens[1]));
          if (! imported.is_absolute ())
            throw format ("Can only import files with absolute paths, not '{1}'.", imported._data);

          if (! imported.readable ())
            throw format ("Could not read imported file '{1}'.", imported._data);

          load (imported._data, nest + 1);
        }

        // Top-level settings:
        //   <name> '=' <value>
        else if (tokens.size () >= 3 &&
                 std::get <0> (tokens[1]) == "=")
        {
          // Extract the words from the 3rd - Nth tuple.
          std::vector <std::string> words;
          for (const auto& token : std::vector <std::tuple <std::string, Lexer::Type>> (tokens.begin () + 2, tokens.end ()))
            words.push_back (std::get <0> (token));

          set (firstWord, join (" ", words));
        }

        // Top-level settings, with no value:
        //   <name> '='
        else if (tokens.size () == 2 &&
                 std::get <0> (tokens[1]) == "=")
        {
          set (firstWord, "");
        }

        // Admit defeat.
        else
          throw format ("Unrecognized construct: {1}", line);
      }
    }
  }

  if (ruleDef != "")
    parseRule (ruleDef);
}

////////////////////////////////////////////////////////////////////////////////
void Rules::parseRule (const std::string& input)
{
  // Break the rule def into lines.
  auto lines = split (input, '\n');

  // Tokenize the first line.
  std::vector <std::string> tokens;
  std::string token;
  Lexer::Type type;
  Lexer lexer (lines[0]);
  while (lexer.token (token, type))
    tokens.push_back (token);

  // Based on the tokens of the first line, determine which rule ﬆype needs to
  // be parsed.
  if (tokens.size () >= 2 &&
      tokens[0] == "define")
  {
    // define rule xxx:
    if (tokens.size () >= 3 && tokens[1] == "rule")
      parseRuleGeneral (lines);

    // define tag xxx:
    else if (tokens.size () >= 3 && tokens[1] == "tag")
      parseRuleTag (lines);

    // define exclusions:
    else if (tokens.size () == 2 && tokens[1] == "exclusions:")
      parseRuleExclusions (lines);

    // define theme:
    else if (tokens.size () == 2 && tokens[1] == "theme:")
      parseRuleTheme (lines);

    // Error.
    else
      throw format ("Unrecognized rule type '{1}'", join (" ", tokens));
  }
}

////////////////////////////////////////////////////////////////////////////////
void Rules::parseRuleGeneral (const std::vector <std::string>& lines)
{
}

////////////////////////////////////////////////////////////////////////////////
void Rules::parseRuleTheme (const std::vector <std::string>& lines)
{
}

////////////////////////////////////////////////////////////////////////////////
void Rules::parseRuleExclusions (const std::vector <std::string>& lines)
{
}

////////////////////////////////////////////////////////////////////////////////
void Rules::parseRuleTag (const std::vector <std::string>& lines)
{
}

////////////////////////////////////////////////////////////////////////////////
unsigned int Rules::getIndentation (const std::string& line)
{
  auto indent = line.find_first_not_of (" ");
  if (indent == std::string::npos)
    indent = 0;

  return indent;
}

////////////////////////////////////////////////////////////////////////////////
// Tokenize the line. This loses whitespace information and token types.
std::vector <std::string> Rules::tokenizeLine (const std::string& line)
{
  std::vector <std::string> tokens;
  std::string token;
  Lexer::Type type;
  Lexer lexer (line);
  while (lexer.token (token, type))
    tokens.push_back (token);

  return tokens;
}

////////////////////////////////////////////////////////////////////////////////
