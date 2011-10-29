/*
   Vimpc
   Copyright (C) 2010 Nathan Sweetman

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.

   command.cpp - ex mode input handling
   */

#include "command.hpp"

#include <algorithm>
#include <pcrecpp.h>
#include <sstream>

#include "algorithm.hpp"
#include "assert.hpp"
#include "buffers.hpp"
#include "settings.hpp"
#include "vimpc.hpp"
#include "window/console.hpp"
#include "window/error.hpp"
#include "window/songwindow.hpp"

using namespace Ui;

// COMMANDS
Command::Command(Ui::Screen & screen, Mpc::Client & client, Main::Settings & settings) :
   InputMode           (screen),
   Player              (screen, client, settings),
   initTabCompletion_  (true),
   forceCommand_       (false),
   aliasTable_         (),
   commandTable_       (),
   screen_             (screen),
   client_             (client),
   settings_           (settings)
{
   // \todo find a away to add aliases to tab completion
   commandTable_["!mpc"]      = &Command::Mpc;
   commandTable_["add"]       = &Command::Add;
   commandTable_["alias"]     = &Command::Alias;
   commandTable_["clear"]     = &Command::ClearScreen;
   commandTable_["connect"]   = &Command::Connect;
   commandTable_["consume"]   = &Command::Consume;
   commandTable_["delete"]    = &Command::Delete;
   commandTable_["deleteall"] = &Command::DeleteAll;
   commandTable_["disable"]   = &Command::Output<false>;
   commandTable_["echo"]      = &Command::Echo;
   commandTable_["enable"]    = &Command::Output<true>;
   commandTable_["error"]     = &Command::EchoError;
   commandTable_["find"]      = &Command::FindAny;
   commandTable_["findalbum"] = &Command::FindAlbum;
   commandTable_["findartist"]= &Command::FindArtist;
   commandTable_["findsong"]  = &Command::FindSong;
   commandTable_["move"]      = &Command::Move;
   commandTable_["pause"]     = &Command::Pause;
   commandTable_["play"]      = &Command::Play;
   commandTable_["q"]         = &Command::Quit;
   commandTable_["qall"]      = &Command::QuitAll;
   commandTable_["quit"]      = &Command::Quit;
   commandTable_["quitall"]   = &Command::QuitAll;
   commandTable_["random"]    = &Command::Random;
   commandTable_["redraw"]    = &Command::Redraw;
   commandTable_["repeat"]    = &Command::Repeat;
   commandTable_["set"]       = &Command::Set;
   commandTable_["seek"]      = &Command::SeekTo;
   commandTable_["seek+"]     = &Command::Seek<1>;
   commandTable_["seek-"]     = &Command::Seek<-1>;
   commandTable_["single"]    = &Command::Single;
   commandTable_["shuffle"]   = &Command::Shuffle;
   commandTable_["swap"]      = &Command::Swap;
   commandTable_["stop"]      = &Command::Stop;
   commandTable_["volume"]    = &Command::Volume;

   commandTable_["tabfirst"]  = &Command::ChangeToWindow<First>;
   commandTable_["tablast"]   = &Command::ChangeToWindow<Last>;
   commandTable_["tabclose"]  = &Command::HideWindow;
   commandTable_["tabhide"]   = &Command::HideWindow;
   commandTable_["tabmove"]   = &Command::MoveWindow;
   commandTable_["tabrename"] = &Command::RenameWindow;

   commandTable_["rescan"]    = &Command::Rescan;
   commandTable_["update"]    = &Command::Update;

   commandTable_["next"]      = &Command::SkipSong<Player::Next>;
   commandTable_["previous"]  = &Command::SkipSong<Player::Previous>;

   commandTable_["browse"]    = &Command::SetActiveAndVisible<Ui::Screen::Browse>;
   commandTable_["console"]   = &Command::SetActiveAndVisible<Ui::Screen::Console>;
   commandTable_["help"]      = &Command::SetActiveAndVisible<Ui::Screen::Help>;
   commandTable_["library"]   = &Command::SetActiveAndVisible<Ui::Screen::Library>;
   commandTable_["playlist"]  = &Command::SetActiveAndVisible<Ui::Screen::Playlist>;
   commandTable_["outputs"]   = &Command::SetActiveAndVisible<Ui::Screen::Outputs>;
   commandTable_["lists"]     = &Command::SetActiveAndVisible<Ui::Screen::Lists>;

   commandTable_["load"]       = &Command::LoadPlaylist;
   commandTable_["save"]       = &Command::SavePlaylist;
   commandTable_["edit"]       = &Command::LoadPlaylist;
   commandTable_["write"]      = &Command::SavePlaylist;
   commandTable_["toplaylist"] = &Command::ToPlaylist;
}

Command::~Command()
{
}


void Command::Initialise(int input)
{
   initTabCompletion_  = true;

   InputMode::Initialise(input);
}

bool Command::Handle(int const input)
{
   ResetTabCompletion(input);

   return InputMode::Handle(input);
}

void Command::GenerateInputString(int input)
{
   if (input == '\t')
   {
      inputString_ = TabComplete(inputString_);
   }
   else
   {
      InputMode::GenerateInputString(input);
   }
}


bool Command::ExecuteCommand(std::string const & input)
{
   pcrecpp::RE const blankCommand("^\\s*$");
   pcrecpp::RE const multipleCommands("^(\\s*([^;]+)\\s*;\\s*).*$");

   std::string matchString, commandString;
   std::string command, arguments;

   SplitCommand(input, command, arguments);

   std::string fullCommand(command + " " + arguments);

   if (aliasTable_.find(command) != aliasTable_.end())
   {
      fullCommand = aliasTable_[command] + " " + arguments;
   }

   if ((multipleCommands.FullMatch(fullCommand.c_str(), &matchString, &commandString) == true) &&
       (command != "alias")) // In the case of alias we don't actually want to run the commands
   {
      // There are multiple commands seperated gy ';' we have to handle each one
      while (multipleCommands.FullMatch(fullCommand.c_str(), &matchString, &commandString) == true)
      {
         fullCommand = fullCommand.substr(matchString.size(), fullCommand.size());

         if (blankCommand.FullMatch(commandString) == false)
         {
            ExecuteCommand(commandString);
         }
      }

      // The last command does not need a ';'
      if (blankCommand.FullMatch(fullCommand) == false)
      {
         ExecuteCommand(fullCommand);
      }
   }
   else if (aliasTable_.find(command) != aliasTable_.end())
   {
      // We are a single command alias, but this could actually be
      // another alias, so ensure to look this up in the table to
      // by calling this fucking again
      ExecuteCommand(fullCommand);
   }
   else
   {
      // Ignore the connect command when starting up if -h/-p used
      // on the command line
      if (command == "connect" && settings_.SkipConfigConnects())
      {
         return true;
      }

      // Just a normal command
      ExecuteCommand(command, arguments);
   }

   return true;
}


char const * Command::Prompt() const
{
   static char const CommandPrompt[] = ":";
   return CommandPrompt;
}

bool Command::InputStringHandler(std::string input)
{
   return ExecuteCommand(input);
}


void Command::ClearScreen(std::string const & arguments)
{
   Player::ClearScreen();
}

void Command::Pause(std::string const & arguments)
{
   Player::Pause();
}

void Command::Play(std::string const & arguments)
{
   int32_t SongId = atoi(arguments.c_str()) - 1;

   if (SongId >= 0)
   {
      Player::Play(SongId);
   }
   else
   {
      Error(ErrorNumber::InvalidParameter, "Invalid song id");
   }
}

void Command::Add(std::string const & arguments)
{
   client_.Add(arguments);
}

void Command::Delete(std::string const & arguments)
{
   size_t pos = arguments.find_first_of(" ");

   if (pos != std::string::npos)
   {
      uint32_t pos1 = atoi(arguments.substr(0, pos).c_str()) - 1;
      uint32_t pos2 = atoi(arguments.substr(pos + 1).c_str()) - 1;

      client_.Delete(pos1, pos2 + 1);
      Main::Playlist().Remove(((pos1 < pos2) ? pos1 : pos2), ((pos1 < pos2) ? pos2 - pos1 : pos1 - pos2) + 1);
   }
   else
   {
      client_.Delete(atoi(arguments.c_str()) - 1);
      Main::Playlist().Remove(atoi(arguments.c_str()) - 1, 1);
   }
}

void Command::DeleteAll(std::string const & arguments)
{
   client_.Clear();
   Main::Playlist().Clear();
}

template <int Delta>
void Command::Seek(std::string const & arguments)
{
   uint32_t time = 0;
   size_t pos    = arguments.find_first_of(":");

   if (pos != std::string::npos)
   {
      std::string minutes = arguments.substr(0, pos);
      std::string seconds = arguments.substr(pos + 1);
      time = (atoi(minutes.c_str()) * 60) + atoi(seconds.c_str());
   }
   else
   {
      time = atoi(arguments.c_str());
   }

   Player::Seek(Delta * time);
}

void Command::SeekTo(std::string const & arguments)
{
   uint32_t time = 0;
   size_t pos    = arguments.find_first_of(":");

   if (pos != std::string::npos)
   {
      std::string minutes = arguments.substr(0, pos);
      std::string seconds = arguments.substr(pos + 1);
      time = (atoi(minutes.c_str()) * 60) + atoi(seconds.c_str());
   }
   else
   {
      time = atoi(arguments.c_str());
   }

   Player::SeekTo(time);
}

void Command::Quit(std::string const & arguments)
{
   if (settings_.SingleQuit() == true)
   {
      QuitAll(arguments);
   }
   else
   {
      HideWindow(arguments);
   }
}

void Command::QuitAll(std::string const & arguments)
{
   if ((forceCommand_ == true) || (settings_.StopOnQuit() == true))
   {
      Player::Stop();
   }

   Player::Quit();
}

void Command::Volume(std::string const & arguments)
{
   uint32_t Vol = atoi(arguments.c_str());

   if ((Vol <= 100) && (Vol >= 0))
   {
      Player::Volume(Vol);
   }
   else
   {
      Error(ErrorNumber::InvalidParameter, "Invalid volume specified");
   }
}


void Command::Connect(std::string const & arguments)
{
   size_t pos = arguments.find_first_of(" ");
   std::string hostname = arguments.substr(0, pos);
   std::string port     = arguments.substr(pos + 1);

   client_.Connect(hostname, std::atoi(port.c_str()));
}

void Command::Echo(std::string const & echo)
{
   Main::Console().Add(echo);
}

void Command::EchoError(std::string const & arguments)
{
   Error(ErrorNumber::Unknown, arguments);
}


template <bool ON>
void Command::Output(std::string const & arguments)
{
   int32_t output = -1;

   if (Algorithm::isNumeric(arguments.c_str()) == true)
   {
      output = atoi(arguments.c_str());
   }
   else
   {
      for(unsigned int i = 0; i < Main::Outputs().Size(); ++i)
      {
         if (Algorithm::iequals(Main::Outputs().Get(i)->Name(), arguments) == true)
         {
            output = i;
            break;
         }
      }
   }

   if ((output < static_cast<int32_t>(Main::Outputs().Size())) && (output >= 0))
   {
      if (ON == true)
      {
         client_.EnableOutput(Main::Outputs().Get(output));
      }
      else
      {
         client_.DisableOutput(Main::Outputs().Get(output));
      }
   }
   else
   {
      Error(ErrorNumber::NoOutput, "No such output");
   }
}


void Command::LoadPlaylist(std::string const & arguments)
{
   if (arguments != "")
   {
      client_.LoadPlaylist(arguments);
   }
   else
   {
      Error(ErrorNumber::InvalidParameter, "Expected parameter");
   }
}

void Command::SavePlaylist(std::string const & arguments)
{
   if (arguments != "")
   {
      client_.SavePlaylist(arguments);
   }
   else
   {
      Error(ErrorNumber::InvalidParameter, "Expected parameter");
   }
}

void Command::ToPlaylist(std::string const & arguments)
{
   if (arguments != "")
   {
      screen_.ActiveWindow().Save(arguments);
      screen_.Redraw(Ui::Screen::Lists);
   }
   else
   {
      Error(ErrorNumber::InvalidParameter, "Expected parameter");
   }
}

void Command::Find(std::string const & arguments)
{
   if (forceCommand_ == true)
   {
      Main::PlaylistTmp().Clear();
      client_.ForEachSearchResult(Main::PlaylistTmp(), static_cast<void (Mpc::Playlist::*)(Mpc::Song *)>(&Mpc::Playlist::Add));

      if (Main::PlaylistTmp().Size() == 0)
      {
         Error(ErrorNumber::FindNoResults, "Find: no results matching this pattern found");
      }
      else
      {
         client_.StartCommandList();

         for (uint32_t i = 0; i < Main::PlaylistTmp().Size(); ++i)
         {
            client_.Add(Main::PlaylistTmp().Get(i));
            Main::Playlist().Add(Main::PlaylistTmp().Get(i));
         }

         client_.SendCommandList();
      }
   }
   else
   {
      SongWindow * window = screen_.CreateSongWindow(arguments);
      client_.ForEachSearchResult(window->Buffer(), static_cast<void (Main::Buffer<Mpc::Song *>::*)(Mpc::Song *)>(&Mpc::Browse::Add));

      if (window->ContentSize() >= 0)
      {
         screen_.SetActiveAndVisible(screen_.GetWindowFromName(window->Name()));
      }
      else
      {
         screen_.SetVisible(screen_.GetWindowFromName(window->Name()), false);
         Error(ErrorNumber::FindNoResults, "Find: no results matching this pattern found");
      }
   }
}

void Command::FindAny(std::string const & arguments)
{
   client_.SearchAny(arguments);
   Find("F:" + arguments);
}

void Command::FindAlbum(std::string const & arguments)
{
   client_.SearchAlbum(arguments);
   Find("FAL:" + arguments);
}

void Command::FindArtist(std::string const & arguments)
{
   client_.SearchArtist(arguments);
   Find("FAR:" + arguments);
}

void Command::FindSong(std::string const & arguments)
{
   client_.SearchSong(arguments);
   Find("FS:" + arguments);
}

void Command::Random(std::string const & arguments)
{
   if (arguments.empty() == false)
   {
      bool const value = (arguments.compare("on") == 0);
      Player::SetRandom(value);
   }
   else
   {
      Player::ToggleRandom();
   }
}

void Command::Repeat(std::string const & arguments)
{
   if (arguments.empty() == false)
   {
      bool const value = (arguments.compare("on") == 0);
      Player::SetRepeat(value);
   }
   else
   {
      Player::ToggleRepeat();
   }
}

void Command::Single(std::string const & arguments)
{
   if (arguments.empty() == false)
   {
      bool const value = (arguments.compare("on") == 0);
      Player::SetSingle(value);
   }
   else
   {
      Player::ToggleSingle();
   }
}

void Command::Consume(std::string const & arguments)
{
   if (arguments.empty() == false)
   {
      bool const value = (arguments.compare("on") == 0);
      Player::SetConsume(value);
   }
   else
   {
      Player::ToggleConsume();
   }
}

void Command::Shuffle(std::string const & arguments)
{
   Player::Shuffle();
}

void Command::Move(std::string const & arguments)
{
   if ((arguments.find(" ") != string::npos))
   {
      int32_t position1 = atoi(arguments.substr(0, arguments.find(" ")).c_str());
      int32_t position2 = atoi(arguments.substr(arguments.find(" ") + 1).c_str());

      if (position1 >= screen_.ActiveWindow().ContentSize())
      {
         position1 = Main::Playlist().Size();
      }
      else if (position1 <= 1)
      {
         position1 = 1;
      }

      if (position2 >= screen_.ActiveWindow().ContentSize())
      {
         position2 = Main::Playlist().Size();
      }
      else if (position2 <= 1)
      {
         position2 = 1;
      }

      client_.Move(position1 - 1, position2 - 1);

      Mpc::Song * song = Main::Playlist().Get(position1 - 1);
      Main::Playlist().Remove(position1 - 1, 1);
      Main::Playlist().Add(song, position2 - 1);
      screen_.Update();
   }
   else
   {
      Error(ErrorNumber::InvalidParameter, "Expected two arguments");
   }
}

void Command::Swap(std::string const & arguments)
{
   if ((arguments.find(" ") != string::npos))
   {
      std::string position1 = arguments.substr(0, arguments.find(" "));
      std::string position2 = arguments.substr(arguments.find(" ") + 1);
      client_.Swap(atoi(position1.c_str()) - 1, atoi(position2.c_str()) - 1);
   }
   else
   {
      Error(ErrorNumber::InvalidParameter, "Expected two arguments");
   }
}

void Command::Redraw(std::string const & arguments)
{
   Player::Redraw();
}

void Command::Stop(std::string const & arguments)
{
   Player::Stop();
}

void Command::Rescan(std::string const & arguments)
{
   Player::Rescan();
}

void Command::Update(std::string const & arguments)
{
   Player::Update();
}


//Implementation of skipping functions
template <Ui::Player::Skip SKIP>
void Command::SkipSong(std::string const & arguments)
{
   uint32_t count = atoi(arguments.c_str());
   count = (count == 0) ? 1 : count;
   Player::SkipSong(SKIP, count);
}


//Implementation of window change function
template <Ui::Screen::MainWindow MAINWINDOW>
void Command::SetActiveAndVisible(std::string const & arguments)
{
   screen_.SetActiveAndVisible(static_cast<int32_t>(MAINWINDOW));
}

template <Command::Location LOCATION>
void Command::ChangeToWindow(std::string const & arguments)
{
   uint32_t active = 0;

   if (LOCATION == Last)
   {
      active = screen_.VisibleWindows() - 1;
   }

   screen_.SetActiveWindow(active);
}

void Command::HideWindow(std::string const & arguments)
{
   if (arguments == "")
   {
      screen_.SetVisible(screen_.GetActiveWindow(), false);
   }
   else
   {
      int32_t window = screen_.GetWindowFromName(arguments);

      if (window != Ui::Screen::Unknown)
      {
         screen_.SetVisible(window, false);
      }
      else
      {
         Error(ErrorNumber::DoesNotExist, "No such tab/window");
      }
   }

   if (screen_.VisibleWindows() == 0)
   {
      Player::Quit();
   }
}

void Command::MoveWindow(std::string const & arguments)
{
   screen_.MoveWindow(atoi(arguments.c_str()));
}

void Command::RenameWindow(std::string const & arguments)
{
   if ((arguments.find(" ") != string::npos))
   {
      std::string oldname = arguments.substr(0, arguments.find(" "));
      std::string newname = arguments.substr(arguments.find(" ") + 1);

      int32_t id = screen_.GetWindowFromName(oldname);

      if (id != Ui::Screen::Unknown)
      {
         screen_.Window(id).SetName(newname);
      }
      else
      {
         Error(ErrorNumber::DoesNotExist, "No such tab/window");
      }
   }
   else
   {
      screen_.ActiveWindow().SetName(arguments);
   }
}


bool Command::ExecuteCommand(std::string command, std::string const & arguments)
{
   pcrecpp::RE const forceCheck("^.*!$");

   forceCommand_   = false;

   if (forceCheck.FullMatch(command))
   {
      forceCommand_  = true;
      command        = command.substr(0, command.length() - 1);
   }

   // If we can't find the exact command, look for a unique command that starts
   // with the input command
   std::string commandToExecute  = command;
   bool        matchingCommand   = (commandTable_.find(commandToExecute) != commandTable_.end());
   uint32_t    validCommandCount = 0;

   if (matchingCommand == false)
   {
      for (CommandTable::const_iterator it = commandTable_.begin(); it != commandTable_.end(); ++it)
      {
         if (command.compare(it->first.substr(0, command.length())) == 0)
         {
            ++validCommandCount;
            commandToExecute = it->first;
         }
      }

      matchingCommand = (validCommandCount == 1);
   }

   // If we have found a command execute it, with \p arguments
   if (matchingCommand == true)
   {
      CommandTable::const_iterator const it = commandTable_.find(commandToExecute);
      CommandFunction const commandFunction = it->second;

      (*this.*commandFunction)(arguments);
   }
   else if (validCommandCount > 1)
   {
      Error(ErrorNumber::CommandAmbiguous, "Command is ambigous, please be more specific: " + command);
   }
   else
   {
      Error(ErrorNumber::CommandNonexistant, "Command not found: " + command);
   }

   forceCommand_ = false;

   return true;
}

void Command::SplitCommand(std::string const & input, std::string & command, std::string & arguments)
{
   std::stringstream commandStream(input);
   std::getline(commandStream, command,   ' ');
   std::getline(commandStream, arguments, '\n');
}

void Command::Set(std::string const & arguments)
{
   settings_.Set(arguments);
}

void Command::Mpc(std::string const & arguments)
{
   static uint32_t const bufferSize = 512;
   char   buffer[bufferSize];
   char   port[8];

   // \todo redirect stderr results into the console window too
   snprintf(port, 8, "%u", client_.Port());

   // Ensure that we use the same mpd_host and port for mpc that
   // we are using but still allow the person running the command
   // to do -h and -p flags
   std::string const command("MPD_HOST=" + client_.Hostname() + " MPD_PORT=" + std::string(port) +
                             " mpc " + arguments);

   Main::Console().Add("> mpc " + arguments);

   FILE * const mpcOutput = popen(command.c_str(), "r");

   if (mpcOutput != NULL)
   {
      while (fgets(buffer, bufferSize - 1, mpcOutput) != NULL)
      {
         Main::Console().Add(buffer);
      }

      pclose(mpcOutput);
   }
   else
   {
      Error(ErrorNumber::ExternalProgramError, "Executing program mpc failed");
   }
}

void Command::Alias(std::string const & input)
{
   std::string command, arguments;

   SplitCommand(input, command, arguments);

   aliasTable_[command] = arguments;
}

void Command::ResetTabCompletion(int input)
{
   if (input != '\t')
   {
      initTabCompletion_ = true;
   }
}


std::string Command::TabComplete(std::string const & command)
{
   static std::string tabStart(command);
   static CommandTable::iterator tabIterator(commandTable_.begin());

   std::string result;

   if (initTabCompletion_ == true)
   {
      initTabCompletion_ = false;
      tabIterator        = commandTable_.begin();
      tabStart           = command;
   }

   tabIterator = find_if(tabIterator, commandTable_.end(), TabCompletionMatch(tabStart));

   if (tabIterator != commandTable_.end())
   {
      result = tabIterator->first;
      ++tabIterator;
   }
   else
   {
      initTabCompletion_ = true;
      result             = tabStart;
   }

   return result;
}

