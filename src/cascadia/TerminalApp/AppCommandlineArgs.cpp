// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "AppCommandlineArgs.h"
#include "ActionArgs.h"

#include <LibraryResources.h>

using namespace winrt::TerminalApp;
using namespace TerminalApp;

// Either a ; at the start of a line, or a ; preceeded by any non-\ char.
// We need \\\\ here, to have an escaped backslash in the actual regex itself.
const std::wregex AppCommandlineArgs::_commandDelimiterRegex{ L"^;|[^\\\\];" };

AppCommandlineArgs::AppCommandlineArgs()
{
    _buildParser();
    _resetStateToDefault();
}

// Method Description:
// - Attempt to parse a given command as a single commandline. If the command
//   doesn't have a subcommand, we'll try parsing the commandline again, as a
//   new-tab command.
// - Actions generated by this command are added to our _startupActions list.
// Arguments:
// - command: The individual commandline to parse as a command.
// Return Value:
// - 0 if the commandline was successfully parsed
int AppCommandlineArgs::ParseCommand(const Commandline& command)
{
    const int argc = gsl::narrow_cast<int>(command.Argc());
    const auto argv = command.Argv();

    // Revert our state to the initial state. As this function can be called
    // multiple times during the parsing of a single commandline (once for each
    // sub-command), we don't want the leftover state from previous calls to
    // pollute this run's state.
    _resetStateToDefault();

    try
    {
        // Manually check for the "/?" or "-?" flags, to manually trigger the help text.
        if (argc == 2 && (NixHelpFlag == argv[1] || WindowsHelpFlag == argv[1]))
        {
            throw CLI::CallForHelp();
        }
        // Clear the parser's internal state
        _app.clear();

        // attempt to parse the commandline
        _app.parse(argc, argv);

        // If we parsed the commandline, and _no_ subcommands were provided, try
        // parsing again as a "new-tab" command.
        if (_noCommandsProvided())
        {
            _newTabCommand->clear();
            _newTabCommand->parse(argc, argv);
        }
    }
    catch (const CLI::CallForHelp& e)
    {
        return _handleExit(_app, e);
    }
    catch (const CLI::ParseError& e)
    {
        // If we parsed the commandline, and _no_ subcommands were provided, try
        // parsing again as a "new-tab" command.
        if (_noCommandsProvided())
        {
            try
            {
                _newTabCommand->clear();
                _newTabCommand->parse(argc, argv);
            }
            catch (const CLI::ParseError& e)
            {
                return _handleExit(*_newTabCommand, e);
            }
        }
        else
        {
            return _handleExit(_app, e);
        }
    }
    return 0;
}

// Method Description:
// - Calls App::exit() for the provided command, and collects it's output into
//   our _exitMessage buffer.
// Arguments:
// - command: Either the root App object, or a subcommand for which to call exit() on.
// - e: the CLI::Error to process as the exit reason for parsing.
// Return Value:
// - 0 if the command exited successfully
int AppCommandlineArgs::_handleExit(const CLI::App& command, const CLI::Error& e)
{
    // Create some streams to collect the output that would otherwise go to stdout.
    std::ostringstream out;
    std::ostringstream err;
    const auto result = command.exit(e, out, err);
    // I believe only CallForHelp will return 0
    if (result == 0)
    {
        _exitMessage = out.str();
    }
    else
    {
        _exitMessage = err.str();
    }
    return result;
}

// Method Description:
// - Add each subcommand and options to the commandline parser.
// Arguments:
// - <none>
// Return Value:
// - <none>
void AppCommandlineArgs::_buildParser()
{
    _buildNewTabParser();
    _buildSplitPaneParser();
    _buildFocusTabParser();
}

// Method Description:
// - Adds the `new-tab` subcommand and related options to the commandline parser.
// Arguments:
// - <none>
// Return Value:
// - <none>
void AppCommandlineArgs::_buildNewTabParser()
{
    _newTabCommand = _app.add_subcommand("new-tab", RSA_(L"NewTabCommandDescription"));
    _addNewTerminalArgs(_newTabCommand);
    _newTabCommand->callback([&, this]() {
        // Buld the NewTab action from the values we've parsed on the commandline.
        auto newTabAction = winrt::make_self<implementation::ActionAndArgs>();
        newTabAction->Action(ShortcutAction::NewTab);
        auto args = winrt::make_self<implementation::NewTabArgs>();
        args->TerminalArgs(_getNewTerminalArgs());
        newTabAction->Args(*args);
        _startupActions.push_back(*newTabAction);
    });
}

// Method Description:
// - Adds the `split-pane` subcommand and related options to the commandline parser.
// Arguments:
// - <none>
// Return Value:
// - <none>
void AppCommandlineArgs::_buildSplitPaneParser()
{
    _newPaneCommand = _app.add_subcommand("split-pane", RSA_(L"SplitPaneCommandDescription"));
    _addNewTerminalArgs(_newPaneCommand);
    auto* horizontalOpt = _newPaneCommand->add_flag("-H,--horizontal",
                                                    _splitHorizontal,
                                                    RSA_(L"SplitPaneHorizontalFlagDescription"));
    auto* verticalOpt = _newPaneCommand->add_flag("-V,--vertical",
                                                  _splitVertical,
                                                  RSA_(L"SplitPaneVerticalFlagDescription"));
    verticalOpt->excludes(horizontalOpt);

    _newPaneCommand->callback([&, this]() {
        // Buld the SplitPane action from the values we've parsed on the commandline.
        auto splitPaneActionAndArgs = winrt::make_self<implementation::ActionAndArgs>();
        splitPaneActionAndArgs->Action(ShortcutAction::SplitPane);
        auto args = winrt::make_self<implementation::SplitPaneArgs>();
        args->TerminalArgs(_getNewTerminalArgs());

        if (_splitHorizontal)
        {
            args->SplitStyle(SplitState::Horizontal);
        }
        else
        {
            args->SplitStyle(SplitState::Vertical);
        }

        splitPaneActionAndArgs->Args(*args);
        _startupActions.push_back(*splitPaneActionAndArgs);
    });
}

// Method Description:
// - Adds the `new-tab` subcommand and related options to the commandline parser.
// Arguments:
// - <none>
// Return Value:
// - <none>
void AppCommandlineArgs::_buildFocusTabParser()
{
    _focusTabCommand = _app.add_subcommand("focus-tab", "Move focus to another tab");
    auto* indexOpt = _focusTabCommand->add_option("-t,--target", _focusTabIndex, "Move focus the tab at the given index");
    auto* nextOpt = _focusTabCommand->add_flag("-n,--next",
                                               _focusNextTab,
                                               "Move focus to the next tab");
    auto* prevOpt = _focusTabCommand->add_flag("-p,--previous",
                                               _focusPrevTab,
                                               "Move focus to the previous tab");
    nextOpt->excludes(prevOpt);
    indexOpt->excludes(prevOpt);
    indexOpt->excludes(nextOpt);

    _focusTabCommand->callback([&, this]() {
        // Buld the action from the values we've parsed on the commandline.
        auto focusTabAction = winrt::make_self<implementation::ActionAndArgs>();

        if (_focusTabIndex >= 0)
        {
            focusTabAction->Action(ShortcutAction::SwitchToTab);
            auto args = winrt::make_self<implementation::SwitchToTabArgs>();
            args->TabIndex(_focusTabIndex);
            focusTabAction->Args(*args);
            _startupActions.push_back(*focusTabAction);
        }
        else if (_focusNextTab || _focusPrevTab)
        {
            focusTabAction->Action(_focusNextTab ? ShortcutAction::NextTab : ShortcutAction::PrevTab);
            _startupActions.push_back(*focusTabAction);
        }
    });
}

// Method Description:
// - Add the `NewTerminalArgs` parameters to the given subcommand. This enables
//   that subcommand to support all the properties in a NewTerminalArgs.
// Arguments:
// - subcommand: the command to add the args to.
// Return Value:
// - <none>
void AppCommandlineArgs::_addNewTerminalArgs(CLI::App* subcommand)
{
    subcommand->add_option("-p,--profile",
                           _profileName,
                           "Open with the given profile. Accepts either the name or guid of a profile");
    subcommand->add_option("-d,--startingDirectory",
                           _startingDirectory,
                           "Open in the given directory instead of the profile's set startingDirectory");
    subcommand->add_option("cmdline",
                           _commandline,
                           "Commandline to run in the given profile");
}

// Method Description:
// - Build a NewTerminalArgs instance from the data we've parsed
// Arguments:
// - <none>
// Return Value:
// - A fully initialized NewTerminalArgs corresponding to values we've currently parsed.
NewTerminalArgs AppCommandlineArgs::_getNewTerminalArgs()
{
    auto args = winrt::make_self<implementation::NewTerminalArgs>();
    if (!_profileName.empty())
    {
        args->Profile(winrt::to_hstring(_profileName));
    }

    if (!_startingDirectory.empty())
    {
        args->StartingDirectory(winrt::to_hstring(_startingDirectory));
    }

    if (!_commandline.empty())
    {
        std::string buffer;
        auto i = 0;
        for (auto arg : _commandline)
        {
            if (arg.find(" ") != std::string::npos)
            {
                buffer += "\"";
                buffer += arg;
                buffer += "\"";
            }
            else
            {
                buffer += arg;
            }
            if (i + 1 < _commandline.size())
            {
                buffer += " ";
            }
            i++;
        }
        args->Commandline(winrt::to_hstring(buffer));
    }

    return *args;
}

// Method Description:
// - This function should return true if _no_ subcommands were parsed from the
//   given commandline. In that case, we'll fall back to trying the commandline
//   as a new tab command.
// Arguments:
// - <none>
// Return Value:
// - true if no sub commands were parsed.
bool AppCommandlineArgs::_noCommandsProvided()
{
    return !(*_newTabCommand ||
             *_focusTabCommand ||
             *_newPaneCommand);
}

// Method Description:
// - Reset any state we might have accumulated back to its default values. Since
//   we'll be re-using these members across the parsing of many commandlines, we
//   need to make sure the state from one run doesn't pollute the following one.
// Arguments:
// - <none>
// Return Value:
// - <none>
void AppCommandlineArgs::_resetStateToDefault()
{
    _profileName = "";
    _startingDirectory = "";
    _commandline.clear();

    _splitVertical = false;
    _splitHorizontal = false;

    _focusTabIndex = -1;
    _focusNextTab = false;
    _focusPrevTab = false;
}

// Function Description:
// - Builds a list of Commandline objects for the given argc,argv. Each
//   Commandline represents a single command to parse. These commands can be
//   seperated by ";", which indicates the start of the next commandline. If the
//   user would like to provide ';' in the text of the commandline, they can
//   escape it as "\;".
// Arguments:
// - args: an array of arguments to parse into Commandlines
// Return Value:
// - a list of Commandline objects, where each one represents a single
//   commandline to parse.
std::vector<Commandline> AppCommandlineArgs::BuildCommands(winrt::array_view<const winrt::hstring>& args)
{
    std::vector<Commandline> commands;
    commands.emplace_back(Commandline{});

    // For each arg in argv:
    // Check the string for a delimiter.
    // * If there isn't a delimiter, add the arg to the current commandline.
    // * If there is a delimiter, split the string at that delimiter. Add the
    //   first part of the string to the current command, ansd start a new
    //   command with the second bit.
    for (uint32_t i = 0; i < args.size(); i++)
    {
        _addCommandsForArg(commands, { args.at(i) });
    }

    return commands;
}

// Function Description:
// - Builds a list of Commandline objects for the given argc,argv. Each
//   Commandline represents a single command to parse. These commands can be
//   seperated by ";", which indicates the start of the next commandline. If the
//   user would like to provide ';' in the text of the commandline, they can
//   escape it as "\;".
// Arguments:
// - argc: the number of arguments provided in argv
// - argv: a c-style array of wchar_t strings. These strings can include spaces in them.
// Return Value:
// - a list of Commandline objects, where each one represents a single
//   commandline to parse.
std::vector<Commandline> AppCommandlineArgs::BuildCommands(const int argc, const wchar_t* argv[])
{
    std::vector<Commandline> commands;
    commands.emplace_back(Commandline{});

    // For each arg in argv:
    // Check the string for a delimiter.
    // * If there isn't a delimiter, add the arg to the current commandline.
    // * If there is a delimiter, split the string at that delimiter. Add the
    //   first part of the string to the current command, ansd start a new
    //   command with the second bit.
    for (auto i = 0; i < argc; i++)
    {
        _addCommandsForArg(commands, { argv[i] });
    }

    return commands;
}

// Function Description:
// - Update and append Commandline objects for the given arg to the given list
//   of commands. Each Commandline represents a single command to parse. These
//   commands can be seperated by ";", which indicates the start of the next
//   commandline. If the user would like to provide ';' in the text of the
//   commandline, they can escape it as "\;".
// - As we parse arg, if it doesn't contain a delimiter in it, we'll add it to
//   the last command in commands. Otherwise, we'll generate a new Commandline
//   object for each command in arg.
// Arguments:
// - commands: a list of Commandline objects to modify and append to
// - arg: a single argument that should be parsed into args to append to the
//   current command, or create more Commandlines
// Return Value:
// <none>
void AppCommandlineArgs::_addCommandsForArg(std::vector<Commandline>& commands, std::wstring_view arg)
{
    std::wstring remaining{ arg };
    std::wsmatch match;
    // Keep looking for matches until we've found no unescaped delimiters,
    // or we've hit the end of the string.
    std::regex_search(remaining, match, AppCommandlineArgs::_commandDelimiterRegex);
    do
    {
        if (match.size() == 0)
        {
            // Easy case: no delimiter. Add it to the current command.
            commands.rbegin()->AddArg(remaining);
            break;
        }
        else
        {
            // Harder case: There was a match.
            const bool matchedFirstChar = match.position(0) == 0;
            // If the match was at the beginning of the string, then the
            // next arg should be "", since there was no content before the
            // delimiter. Otherwise, add one, since the regex will include
            // the last character of the string before the delimiter.
            auto delimiterPosition = matchedFirstChar ? match.position(0) : match.position(0) + 1;
            auto nextArg = remaining.substr(0, delimiterPosition);

            if (nextArg != L"")
            {
                commands.rbegin()->AddArg(nextArg);
            }

            // Create a new commandline
            commands.emplace_back(Commandline{});
            commands.rbegin()->AddArg(std::wstring{ L"wt.exe" });

            // Look for the next match in the string, but updating our
            // remaining to be the text after the match.
            remaining = match.suffix().str();
            std::regex_search(remaining, match, AppCommandlineArgs::_commandDelimiterRegex);
        }
    } while (remaining.size() > 0);
}

// Method Description:
// - Returns the deque of actions we've buffered as a result of parsing commands.
// Arguments:
// - <none>
// Return Value:
// - the deque of actions we've buffered as a result of parsing commands.
std::deque<winrt::TerminalApp::ActionAndArgs>& AppCommandlineArgs::GetStartupActions()
{
    return _startupActions;
}

// Method Description:
// - Get the string of text that should be displayed to the user on exit. This
//   is usually helpful for cases where the user entered some sort of invalid
//   commandline. It's additionally also used when the user has requested the
//   help text.
// Arguments:
// - <none>
// Return Value:
// - The help text, or an error message, generated from parsing the input
//   provided by the user.
const std::string& AppCommandlineArgs::GetExitMessage()
{
    return _exitMessage;
}

// Method Description:
// - Ensure that the first command in our list of actions is a NewTab action.
//   This makes sure that if the user passes a commandline like "wt split-pane
//   -H", we _first_ create a new tab, so there's always at least one tab.
// - If the first command in our queue of actions is a NewTab action, this does
//   nothing.
// - This should only be called once - if the first NewTab action is popped from
//   our _startupActions, calling this again will add another.
// Arguments:
// - <none>
// Return Value:
// - <none>
void AppCommandlineArgs::ValidateStartupCommands()
{
    // If we parsed no commands, or the first command we've parsed is not a new
    // tab action, prepend a new-tab command to the front of the list.
    if (_startupActions.size() == 0 ||
        _startupActions.front().Action() != ShortcutAction::NewTab)
    {
        // Buld the NewTab action from the values we've parsed on the commandline.
        auto newTabAction = winrt::make_self<implementation::ActionAndArgs>();
        newTabAction->Action(ShortcutAction::NewTab);
        auto args = winrt::make_self<implementation::NewTabArgs>();
        auto newTerminalArgs = winrt::make_self<implementation::NewTerminalArgs>();
        args->TerminalArgs(*newTerminalArgs);
        newTabAction->Args(*args);
        _startupActions.push_front(*newTabAction);
    }
}
