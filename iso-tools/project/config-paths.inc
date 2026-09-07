
#ifndef SZS_TEXT_CONFIG_PATHS_INC
#define SZS_TEXT_CONFIG_PATHS_INC 1

const char text_config_paths[] =
{
  "\n"
  "[PATHS]\n"
  "# This section defines different paths. Each path may contain any number of\n"
  "# references to other paths by »$(NAME)«. All names are case sensitive.\n"
  "# >>> Please use '/' and not '\\' as directory separator, even for Cygwin. <<<\n"
  "\n"
  "# The base is only a helper path to allow relative paths. It is not used\n"
  "# internally. For Cygwin, it is usually Windows »PROGRAMFILES«. For other\n"
  "# systems it is »/usr/local«.\n"
  "base	= %s\n"
  "\n"
  "# The directory, where the tools will be installed. After installation\n"
  "# it is used for updates. It is usually »$(base)/Wiimm/SZS« for Cygwin\n"
  "# and »$(base)/bin« for other systems.\n"
  "install	= %s\n"
  "\n"
  "# File or directory, where a configuration should be stored on installation.\n"
  "# The filename »wiimms-szs-tools.conf« is appended for directories.\n"
  "# An existing file is scanned and rewritten. If empty, don't create it.\n"
  "config	= %s\n"
  "\n"
  "# The following is a directory list in the order in which the configuration\n"
  "# file is searched. Uncomment a line to override the previous setting.\n"
  "#   config = \"$(xdg_home)/szs/\"	# XDG directory of the user\n"
  "#   config = \"$(home)/.szs/\"	# Home directory of the user\n"
  "#   config = \"$(xdg_etc)/\"	# Standard XDG config-dir for unix based systems\n"
  "#   config = \"/etc/\"		# Standard config-dir for unix based systems\n"
  "#   config = \"$(share)/\"	# Directory of shared files (bad choice)\n"
  "#   config = \"$(install)/\"	# Directory of installation\n"
  "\n"
  "# The directory for shared files. It is usually »$(install)« for Cygwin and\n"
  "# »$(base)/share/szs« for other systems.\n"
  "share	= %s\n"
  "\n"
  "# The directory in which the »titles« files are saved and searched for first.\n"
  "# It is usually »$(share)« for all systems.\n"
  "titles	= %s\n"
  "\n"
};

#endif // SZS_TEXT_CONFIG_PATHS_INC

