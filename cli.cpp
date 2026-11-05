/*---------------------------------------------------------*\
| cli.cpp                                                   |
|                                                           |
|   OpenRGB command line interface                          |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include <vector>
#include <cstring>
#include <string>
#include <tuple>
#include <iostream>
#include <iomanip>
#include "AutoStart.h"
#include "cli.h"
#include "Colors.h"
#include "filesystem.h"
#include "LogManager.h"
#include "NetworkClient.h"
#include "ProfileManager.h"
#include "ResourceManager.h"
#include "RGBController.h"
#include "SettingsManager.h"
#include "JsonUtils.h"
#include <algorithm>
#include <thread>

#ifdef __linux__
#include "DetectionManager.h"
#endif

/*---------------------------------------------------------*\
| Quirk for MSVC; which doesn't support this                |
| case-insensitive function                                 |
\*---------------------------------------------------------*/
#ifdef _WIN32
#include <shellapi.h>
    #define strcasecmp _strcmpi
#endif

using namespace std::chrono_literals;

/*---------------------------------------------------------*\
| Static variables for CLI                                  |
\*---------------------------------------------------------*/
static std::string                  profile_save_name       = "";
static int                          preserve_argc           = 0;
static char**                       preserve_argv           = nullptr;

/*---------------------------------------------------------*\
| Options for configuring devices from CLI                  |
\*---------------------------------------------------------*/
struct DeviceOptions
{
    int             device;
    int             zone            = -1;
    std::vector<std::tuple<unsigned char, unsigned char, unsigned char>> colors;
    std::string     mode;
    unsigned int    speed           = 100;
    unsigned int    brightness      = 100;
    unsigned int    size;
    bool            random_colors   = false;
    bool            hasSize         = false;
    bool            hasOption       = false;

    /*-----------------------------------------------------*\
    | Extended CLI options                                  |
    \*-----------------------------------------------------*/
    int             direction       = -1;
    std::vector<unsigned int> leds;
    bool            save_to_device  = false;
    bool            hasConfig       = false;
    bool            rename_device   = false;
    std::string     device_name;
    bool            rename_zone     = false;
    std::string     zone_name;
    bool            set_segments    = false;
    std::vector<std::tuple<std::string, unsigned int>> segments;
};

struct ServerOptions
{
    bool            start           = false;
    unsigned short  port            = OPENRGB_SDK_PORT;
};

struct Options
{
    std::vector<DeviceOptions>  devices;

    /*-----------------------------------------------------*\
    | If hasDevice is false, devices above is empty and     |
    | allDeviceOptions shall be applied to all available    |
    | devices except in the case that a profile was loaded. |
    \*-----------------------------------------------------*/
    bool                        hasDevice           = false;
    bool                        profile_loaded      = false;
    bool                        action_performed    = false;
    DeviceOptions               allDeviceOptions;
    ServerOptions               servOpts;
};

/*---------------------------------------------------------*\
| Support a common subset of human colors; for easier       |
| typing: https://www.w3.org/TR/css-color-3/#svg-color      |
\*---------------------------------------------------------*/
struct HumanColors { uint32_t rgb; const char* keyword; } static const human_colors[] =
{
    { COLOR_BLACK,                  "black"                 },
    { COLOR_NAVY,                   "navy"                  },
    { COLOR_DARKBLUE,               "darkblue"              },
    { COLOR_MEDIUMBLUE,             "mediumblue"            },
    { COLOR_BLUE,                   "blue"                  },
    { COLOR_DARKGREEN,              "darkgreen"             },
    { COLOR_GREEN,                  "green"                 },
    { COLOR_TEAL,                   "teal"                  },
    { COLOR_DARKCYAN,               "darkcyan"              },
    { COLOR_DEEPSKYBLUE,            "deepskyblue"           },
    { COLOR_DARKTURQUOISE,          "darkturquoise"         },
    { COLOR_MEDIUMSPRINGGREEN,      "mediumspringgreen"     },
    { COLOR_LIME,                   "lime"                  },
    { COLOR_AQUA,                   "aqua"                  },
    { COLOR_SPRINGGREEN,            "springgreen"           },
    { COLOR_MIDNIGHTBLUE,           "midnightblue"          },
    { COLOR_CYAN,                   "cyan"                  },
    { COLOR_LIGHTSEAGREEN,          "lightseagreen"         },
    { COLOR_DODGERBLUE,             "dodgerblue"            },
    { COLOR_SEAGREEN,               "seagreen"              },
    { COLOR_FORESTGREEN,            "forestgreen"           },
    { COLOR_DARKSLATEGREY,          "darkslategrey"         },
    { COLOR_DARKSLATEGRAY,          "darkslategray"         },
    { COLOR_MEDIUMSEAGREEN,         "mediumseagreen"        },
    { COLOR_LIMEGREEN,              "limegreen"             },
    { COLOR_ROYALBLUE,              "royalblue"             },
    { COLOR_TURQUOISE,              "turquoise"             },
    { COLOR_DARKSLATEBLUE,          "darkslateblue"         },
    { COLOR_STEELBLUE,              "steelblue"             },
    { COLOR_INDIGO,                 "indigo"                },
    { COLOR_MEDIUMTURQUOISE,        "mediumturquoise"       },
    { COLOR_CADETBLUE,              "cadetblue"             },
    { COLOR_DARKOLIVEGREEN,         "darkolivegreen"        },
    { COLOR_MEDIUMAQUAMARINE,       "mediumaquamarine"      },
    { COLOR_CORNFLOWERBLUE,         "cornflowerblue"        },
    { COLOR_DIMGREY,                "dimgrey"               },
    { COLOR_DIMGRAY,                "dimgray"               },
    { COLOR_OLIVEDRAB,              "olivedrab"             },
    { COLOR_SLATEBLUE,              "slateblue"             },
    { COLOR_SLATEGREY,              "slategrey"             },
    { COLOR_SLATEGRAY,              "slategray"             },
    { COLOR_LIGHTSLATEGREY,         "lightslategrey"        },
    { COLOR_LIGHTSLATEGRAY,         "lightslategray"        },
    { COLOR_LAWNGREEN,              "lawngreen"             },
    { COLOR_MEDIUMSLATEBLUE,        "mediumslateblue"       },
    { COLOR_AQUAMARINE,             "aquamarine"            },
    { COLOR_CHARTREUSE,             "chartreuse"            },
    { COLOR_PURPLE,                 "purple"                },
    { COLOR_MAROON,                 "maroon"                },
    { COLOR_ELECTRIC_ULTRAMARINE,   "electricultramarine"   },
    { COLOR_OLIVE,                  "olive"                 },
    { COLOR_GRAY,                   "gray"                  },
    { COLOR_GREY,                   "grey"                  },
    { COLOR_SKYBLUE,                "skyblue"               },
    { COLOR_LIGHTSKYBLUE,           "lightskyblue"          },
    { COLOR_BLUEVIOLET,             "blueviolet"            },
    { COLOR_DARKRED,                "darkred"               },
    { COLOR_DARKMAGENTA,            "darkmagenta"           },
    { COLOR_SADDLEBROWN,            "saddlebrown"           },
    { COLOR_DARKSEAGREEN,           "darkseagreen"          },
    { COLOR_LIGHTGREEN,             "lightgreen"            },
    { COLOR_MEDIUMPURPLE,           "mediumpurple"          },
    { COLOR_DARKVIOLET,             "darkviolet"            },
    { COLOR_PALEGREEN,              "palegreen"             },
    { COLOR_DARKORCHID,             "darkorchid"            },
    { COLOR_YELLOWGREEN,            "yellowgreen"           },
    { COLOR_SIENNA,                 "sienna"                },
    { COLOR_BROWN,                  "brown"                 },
    { COLOR_DARKGRAY,               "darkgray"              },
    { COLOR_DARKGREY,               "darkgrey"              },
    { COLOR_LIGHTBLUE,              "lightblue"             },
    { COLOR_GREENYELLOW,            "greenyellow"           },
    { COLOR_PALETURQUOISE,          "paleturquoise"         },
    { COLOR_LIGHTSTEELBLUE,         "lightsteelblue"        },
    { COLOR_POWDERBLUE,             "powderblue"            },
    { COLOR_FIREBRICK,              "firebrick"             },
    { COLOR_DARKGOLDENROD,          "darkgoldenrod"         },
    { COLOR_MEDIUMORCHID,           "mediumorchid"          },
    { COLOR_ROSYBROWN,              "rosybrown"             },
    { COLOR_DARKKHAKI,              "darkkhaki"             },
    { COLOR_SILVER,                 "silver"                },
    { COLOR_MEDIUMVIOLETRED,        "mediumvioletred"       },
    { COLOR_INDIANRED,              "indianred"             },
    { COLOR_PERU,                   "peru"                  },
    { COLOR_CHOCOLATE,              "chocolate"             },
    { COLOR_TAN,                    "tan"                   },
    { COLOR_LIGHTGRAY,              "lightgray"             },
    { COLOR_LIGHTGREY,              "lightgrey"             },
    { COLOR_THISTLE,                "thistle"               },
    { COLOR_ORCHID,                 "orchid"                },
    { COLOR_GOLDENROD,              "goldenrod"             },
    { COLOR_PALEVIOLETRED,          "palevioletred"         },
    { COLOR_CRIMSON,                "crimson"               },
    { COLOR_GAINSBORO,              "gainsboro"             },
    { COLOR_PLUM,                   "plum"                  },
    { COLOR_BURLYWOOD,              "burlywood"             },
    { COLOR_LIGHTCYAN,              "lightcyan"             },
    { COLOR_LAVENDER,               "lavender"              },
    { COLOR_DARKSALMON,             "darksalmon"            },
    { COLOR_VIOLET,                 "violet"                },
    { COLOR_PALEGOLDENROD,          "palegoldenrod"         },
    { COLOR_LIGHTCORAL,             "lightcoral"            },
    { COLOR_KHAKI,                  "khaki"                 },
    { COLOR_ALICEBLUE,              "aliceblue"             },
    { COLOR_HONEYDEW,               "honeydew"              },
    { COLOR_AZURE,                  "azure"                 },
    { COLOR_SANDYBROWN,             "sandybrown"            },
    { COLOR_WHEAT,                  "wheat"                 },
    { COLOR_BEIGE,                  "beige"                 },
    { COLOR_WHITESMOKE,             "whitesmoke"            },
    { COLOR_MINTCREAM,              "mintcream"             },
    { COLOR_GHOSTWHITE,             "ghostwhite"            },
    { COLOR_SALMON,                 "salmon"                },
    { COLOR_ANTIQUEWHITE,           "antiquewhite"          },
    { COLOR_LINEN,                  "linen"                 },
    { COLOR_LIGHTGOLDENRODYELLOW,   "lightgoldenrodyellow"  },
    { COLOR_OLDLACE,                "oldlace"               },
    { COLOR_RED,                    "red"                   },
    { COLOR_FUCHSIA,                "fuchsia"               },
    { COLOR_MAGENTA,                "magenta"               },
    { COLOR_DEEPPINK,               "deeppink"              },
    { COLOR_ORANGERED,              "orangered"             },
    { COLOR_TOMATO,                 "tomato"                },
    { COLOR_HOTPINK,                "hotpink"               },
    { COLOR_CORAL,                  "coral"                 },
    { COLOR_DARKORANGE,             "darkorange"            },
    { COLOR_LIGHTSALMON,            "lightsalmon"           },
    { COLOR_ORANGE,                 "orange"                },
    { COLOR_LIGHTPINK,              "lightpink"             },
    { COLOR_PINK,                   "pink"                  },
    { COLOR_GOLD,                   "gold"                  },
    { COLOR_PEACHPUFF,              "peachpuff"             },
    { COLOR_NAVAJOWHITE,            "navajowhite"           },
    { COLOR_MOCCASIN,               "moccasin"              },
    { COLOR_BISQUE,                 "bisque"                },
    { COLOR_MISTYROSE,              "mistyrose"             },
    { COLOR_BLANCHEDALMOND,         "blanchedalmond"        },
    { COLOR_PAPAYAWHIP,             "papayawhip"            },
    { COLOR_LAVENDERBLUSH,          "lavenderblush"         },
    { COLOR_SEASHELL,               "seashell"              },
    { COLOR_CORNSILK,               "cornsilk"              },
    { COLOR_LEMONCHIFFON,           "lemonchiffon"          },
    { COLOR_FLORALWHITE,            "floralwhite"           },
    { COLOR_SNOW,                   "snow"                  },
    { COLOR_YELLOW,                 "yellow"                },
    { COLOR_LIGHTYELLOW,            "lightyellow"           },
    { COLOR_IVORY,                  "ivory"                 },
    { COLOR_WHITE,                  "white"                 },
    { 0,                            NULL                    }
};

bool ParseColors(std::string colors_string, DeviceOptions *options)
{
    while(colors_string.length() > 0)
    {
        size_t      rgb_end = colors_string.find_first_of(',');
        std::string color   = colors_string.substr(0, rgb_end);
        int32_t     rgb     = 0;
        bool        parsed  = false;

        if(color.length() <= 0)
        {
            break;
        }

        /*-------------------------------------------------*\
        | This will set correct color mode for modes with   |
        | a MODE_COLORS_RANDOM else generate a random color |
        | from the human_colors list above                  |
        \*-------------------------------------------------*/
        if(color == "random")
        {
            options->random_colors      = true;
            srand((unsigned int)time(NULL));
            int index                   = rand() % (sizeof(human_colors) / sizeof(human_colors[0])) + 1; //Anything other than black
            rgb                         = human_colors[index].rgb;
            parsed                      = true;
        }
        else
        {
            /*---------------------------------------------*\
            | Try interpreting it as text; as human         |
            | keywords, otherwise strtoul() will pick up    |
            | 'darkgreen' as 0xDA                           |
            \*---------------------------------------------*/
            for(const struct HumanColors *hc = human_colors; hc->keyword != NULL; hc++)
            {
                if(strcasecmp(hc->keyword, color.c_str()) != 0)
                {
                    continue;
                }

                rgb                     = hc->rgb; parsed = true;

                break;
            }
        }

        /*-------------------------------------------------*\
        | No luck, try interpreting it as an hexadecimal    |
        | number instead                                    |
        \*-------------------------------------------------*/
        if(!parsed)
        {
            if(color.length() == 6)
            {
                const char* colorptr    = color.c_str();
                char*       endptr      = NULL;

                rgb                     = strtoul(colorptr, &endptr, 16);

                /*-----------------------------------------*\
                | Check that strtoul() has advanced the     |
                | read pointer until the end (NULL          |
                | terminator), that means it has read the   |
                | whole thing                               |
                \*-----------------------------------------*/
                if(colorptr != endptr && endptr && *endptr == '\0')
                {
                    parsed              = true;
                }
            }
        }

        /*-------------------------------------------------*\
        | We got it, save the 32-bit integer as a tuple of  |
        | three RGB bytes                                   |
        \*-------------------------------------------------*/
        if(parsed)
        {
            options->colors.push_back(std::make_tuple(
                (rgb >> (8 * 2)) & 0xFF, /* RR.... */
                (rgb >> (8 * 1)) & 0xFF, /* ..GG.. */
                (rgb >> (8 * 0)) & 0xFF  /* ....BB */
            ));
        }
        else
        {
            std::cout << "Error: Unknown color: '" + color + "', skipping." << std::endl;
        }

        /*-------------------------------------------------*\
        | If there are no more colors                       |
        \*-------------------------------------------------*/
        if(rgb_end == std::string::npos)
        {
            break;
        }

        /*-------------------------------------------------*\
        | Remove the current color and the next color's     |
        | leading comma                                     |
        \*-------------------------------------------------*/
        colors_string                   = colors_string.substr(color.length() + 1);
    }

    return options->colors.size() > 0;
}

unsigned int ParseMode(DeviceOptions& options, std::vector<RGBController *> &rgb_controllers)
{
    /*-----------------------------------------------------*\
    | No need to check if --mode wasn't passed              |
    \*-----------------------------------------------------*/
    if(options.mode.size() == 0)
    {
        return rgb_controllers[options.device]->GetActiveMode();
    }

    /*-----------------------------------------------------*\
    | Search through all of the device modes and see if     |
    | there is a match.  If no match is found, print an     |
    | error message.                                        |
    \*-----------------------------------------------------*/
    for(unsigned int mode_idx = 0; mode_idx < rgb_controllers[options.device]->GetModeCount(); mode_idx++)
    {
        if(strcasecmp(rgb_controllers[options.device]->GetModeName(mode_idx).c_str(), options.mode.c_str()) == 0)
        {
            return mode_idx;
        }
    }

    std::cout << "Error: Mode '" + options.mode + "' not available for device '" + rgb_controllers[options.device]->GetDisplayName() + "'" << std::endl;
    return false;
}

DeviceOptions* GetDeviceOptionsForDevID(Options *opts, int device)
{
    if(device == -1)
    {
        return &opts->allDeviceOptions;
    }

    for(unsigned int i = 0; i < opts->devices.size(); i++)
    {
        if (opts->devices[i].device == device)
        {
            return &opts->devices[i];
        }
    }

    std::cout << "Internal error: Tried setting an option on a device that wasn't specified" << std::endl;
    abort();
}

std::string QuoteIfNecessary(std::string str)
{
    if(str.find(' ') == std::string::npos)
    {
        return str;
    }
    else
    {
        return "'" + str + "'";
    }
}

/*---------------------------------------------------------*\
| Option processing functions                               |
\*---------------------------------------------------------*/
void OptionHelp()
{
    std::string help_text;
    help_text += "OpenRGB ";
    help_text += VERSION_STRING;
    help_text += ", an open-source RGB control utility\n";
    help_text += "Usage: OpenRGB (--device [--mode] [--color])...\n";
    help_text += "\n";
    help_text += "Options:\n";
    help_text += "--gui                                    Shows the GUI. GUI also appears when not passing any parameters\n";
    help_text += "--startminimized                         Starts the GUI minimized to tray. Implies --gui, even if not specified\n";
    help_text += "--client [IP]:[Port]                     Starts an SDK client on the given IP:Port (assumes port 6742 if not specified)\n";
    help_text += "--server                                 Starts the SDK's server\n";
    help_text += "--server-host                            Sets the SDK's server host. Default: 127.0.0.1\n";
    help_text += "--server-port                            Sets the SDK's server port. Default: 6742 (1024-65535)\n";
    help_text += "-l,  --list-devices                      Displays a simple list of active devices\n";
    help_text += "-ld  --list-detailed                     Displays a detailed list of active devices\n";
    help_text += "-lp  --list-profiles                     Displays a list of available profiles\n";
    help_text += "-lx  --list-extended                     Displays devices with mode capabilities, zone LED ranges, sizes and segments\n";
    help_text += "-d,  --device [0-9 | \"name\"]             Selects device to apply colors and/or effect to, or applies to all devices if omitted\n";
    help_text += "                                           Basic string search is implemented 3 characters or more\n";
    help_text += "                                           Can be specified multiple times with different modes and colors\n";
    help_text += "-z,  --zone [0-9]                        Selects zone to apply colors and/or sizes to, or applies to all zones in device if omitted\n";
    help_text += "                                           Must be specified after specifying a device\n";
    help_text += "-c,  --color [random | FFFFF,00AAFF ...] Sets colors on each device directly if no effect is specified, and sets the effect color if an effect is specified\n";
    help_text += "                                           If there are more LEDs than colors given, the last color will be applied to the remaining LEDs\n";
    help_text += "-m,  --mode [breathing | static | ...]   Sets the mode to be applied, check --list-devices to see which modes are supported on your device\n";
    help_text += "-b,  --brightness [0-100]                Sets the brightness as a percentage if the mode supports brightness\n";
    help_text += "-s,  --speed [0-100]                     Sets the speed as a percentage if the mode supports speed\n";
    help_text += "-sz, --size [0-N]                        Sets the new size of the specified device zone.\n";
    help_text += "                                           Must be specified after specifying a zone.\n";
    help_text += "                                           If the specified size is out of range, or the zone does not offer resizing capability, the size will not be changed\n";
    help_text += "-V,  --version                           Display version and software build information\n";
    help_text += "-p,  --profile [name]                    Load the given profile\n";
    help_text += "-sp, --save-profile [name]               Save the given profile\n";
    help_text += "\n";
    help_text += "Extended device options (use after --device, and --zone where noted):\n";
    help_text += "--direction [left | right | up | down | horizontal | vertical | up-left | up-right | down-left | down-right]\n";
    help_text += "                                         Sets the direction of the selected mode, if the mode supports it\n";
    help_text += "--led [0,3,5-9]                          Selects specific LEDs by number for --color, relative to --zone if given\n";
    help_text += "                                           Other LEDs keep their current color. Requires a per-LED mode such as Direct\n";
    help_text += "--save-to-device                         Saves the applied mode to the device's own memory (the GUI's 'Save to Device')\n";
    help_text += "--rename-device [name]                   Sets a custom name for the selected device. Use \"\" to restore the original name\n";
    help_text += "--rename-zone [name]                     Sets a custom name for the selected zone (requires --zone). Use \"\" to restore it\n";
    help_text += "--segments [name:size,name:size,...]     Replaces the segments of the selected zone (requires --zone). Use \"\" to clear them\n";
    help_text += "--rescan                                 Rescans for devices (in the running service, if connected to one)\n";
    help_text += "\n";
    help_text += "Profile options:\n";
    help_text += "--delete-profile [name]                  Deletes the given profile\n";
    help_text += "--rename-profile [old] [new]             Renames a profile\n";
    help_text += "--list-auto-profiles                     Shows which profile is loaded automatically on each event\n";
    help_text += "--auto-profile [event] [name | off]      Sets the profile loaded automatically on an event:\n";
    help_text += "                                           service-startup, service-shutdown, open, exit, suspend, resume\n";
    help_text += "\n";
    help_text += "Detector and settings options:\n";
    help_text += "--list-detectors [filter]                Lists device detectors and whether they are enabled. Filter is optional\n";
    help_text += "--enable-detector [name]                 Enables a device detector (exact name, or a unique part of it)\n";
    help_text += "--disable-detector [name]                Disables a device detector (exact name, or a unique part of it)\n";
    help_text += "--list-settings                          Lists every settings section and its options with descriptions\n";
    help_text += "--get-setting [section | section.option] Prints the current value of a settings section or option\n";
    help_text += "--set-setting [section.option] [value]   Changes a setting. Value is JSON (true, 5, \"text\") or plain text\n";
    help_text += "--i2c-tools                              Shows the I2C/SMBus Tools page in the GUI. Implies --gui, even if not specified.\n";
    help_text += "                                           USE I2C TOOLS AT YOUR OWN RISK! Don't use this option if you don't know what you're doing!\n";
    help_text += "                                           There is a risk of bricking your motherboard, RGB controller, and RAM if you send invalid SMBus/I2C transactions.\n";
    help_text += "--localconfig                            Use the current working directory instead of the global configuration directory.\n";
    help_text += "--config path                            Use a custom path instead of the global configuration directory.\n";
    help_text += "--nodetect                               Do not try to detect hardware at startup.\n";
    help_text += "--noautoconnect                          Do not try to autoconnect to a local server at startup.\n";
    help_text += "--loglevel [0-6 | error | warning ...]   Set the log level (0: fatal to 6: trace).\n";
    help_text += "--print-source                           Print the source code file and line number for each log entry.\n";
    help_text += "-v,  --verbose                           Print log messages to stdout.\n";
    help_text += "-vv, --very-verbose                      Print debug messages and log messages to stdout.\n";
    help_text += "--autostart-check                        Check if OpenRGB starting at login is enabled.\n";
    help_text += "--autostart-disable                      Disable OpenRGB starting at login.\n";
    help_text += "--autostart-enable arguments             Enable OpenRGB to start at login. Requires arguments to give to OpenRGB at login.\n";
#ifdef __linux__
    help_text += "--generate-udev-rules [filename]         Generate the OpenRGB udev rules file and save it to the given filename.\n";
    help_text += "--print-udev-rules                       Print the OpenRGB udev rules to stdout. Can be redirected to a file. No arguments.\n";
#endif

    std::cout << help_text << std::endl;
}

void OptionVersion()
{
    std::string version_text;
    version_text += "OpenRGB ";
    version_text += VERSION_STRING;
    version_text += ", for controlling RGB lighting.\n";
    version_text += "  Version:\t\t ";
    version_text += VERSION_STRING;
    version_text += "\n  Build Date\t\t ";
    version_text += BUILDDATE_STRING;
    version_text += "\n  Git Commit ID\t\t ";
    version_text += GIT_COMMIT_ID;
    version_text += "\n  Git Commit Date\t ";
    version_text += GIT_COMMIT_DATE;
    version_text += "\n  Git Branch\t\t ";
    version_text += GIT_BRANCH;
    version_text += "\n";

    std::cout << version_text << std::endl;
}

void OptionListDevices(std::vector<RGBController *>& rgb_controllers, bool detailed)
{
    ResourceManager::get()->WaitForDetection();

    for(std::size_t controller_idx = 0; controller_idx < rgb_controllers.size(); controller_idx++)
    {
        RGBController *controller = rgb_controllers[controller_idx];

        /*-------------------------------------------------*\
        | Print device name                                 |
        \*-------------------------------------------------*/
        std::cout << controller_idx << ": " << controller->GetDisplayName() << std::endl;

        if(!detailed)
        {
            continue;
        }

        /*-------------------------------------------------*\
        | Print device type                                 |
        \*-------------------------------------------------*/
            std::cout << "  Type:           " << RGBController::DeviceTypeToString(controller->GetDeviceType()) << std::endl;

        /*-------------------------------------------------*\
        | Print device description                          |
        \*-------------------------------------------------*/
        if(!controller->GetDescription().empty())
        {
            std::cout << "  Description:    " << controller->GetDescription() << std::endl;
        }

        /*-------------------------------------------------*\
        | Print device version                              |
        \*-------------------------------------------------*/
        if(!controller->GetVersion().empty())
        {
            std::cout << "  Version:        " << controller->GetVersion() << std::endl;
        }

        /*-------------------------------------------------*\
        | Print device location                             |
        \*-------------------------------------------------*/
        if(!controller->GetLocation().empty())
        {
            std::cout << "  Location:       " << controller->GetLocation() << std::endl;
        }

        /*-------------------------------------------------*\
        | Print device serial                               |
        \*-------------------------------------------------*/
        if(!controller->GetSerial().empty())
        {
            std::cout << "  Serial:         " << controller->GetSerial() << std::endl;
        }

        /*-------------------------------------------------*\
        | Print device modes                                |
        \*-------------------------------------------------*/
        if(controller->GetModeCount() > 0)
        {
            std::cout << "  Modes:";

            int current_mode = controller->GetActiveMode();
            for(unsigned int mode_idx = 0; mode_idx < controller->GetModeCount(); mode_idx++)
            {
                std::string modeStr = QuoteIfNecessary(controller->GetModeName(mode_idx));

                if(current_mode == (int)mode_idx)
                {
                    modeStr = "[" + modeStr + "]";
                }
                std::cout << " " << modeStr;
            }
            std::cout << std::endl;
        }

        /*-------------------------------------------------*\
        | Print device zones                                |
        \*-------------------------------------------------*/
        if(controller->GetZoneCount() > 0)
        {
            std::cout << "  Zones:";

            for(unsigned int zone_idx = 0; zone_idx < controller->GetZoneCount(); zone_idx++)
            {
                std::cout << " " << QuoteIfNecessary(controller->GetZoneDisplayName(zone_idx));
            }
            std::cout << std::endl;
        }

        /*-------------------------------------------------*\
        | Print device LEDs                                 |
        \*-------------------------------------------------*/
        if(controller->GetLEDCount() > 0)
        {
            std::cout << "  LEDs:";

            for(unsigned int led_idx = 0; led_idx < controller->GetLEDCount(); led_idx++)
            {
                std::cout << " " << QuoteIfNecessary(controller->GetLEDName(led_idx));
            }
            std::cout << std::endl;
        }

        std::cout << std::endl;
    }
}

void OptionListProfiles()
{
    ResourceManager::get()->WaitForDetection();
    std::string                 active_profile  = ResourceManager::get()->GetProfileManager()->GetActiveProfile();
    std::vector<std::string>    profile_list    = ResourceManager::get()->GetProfileManager()->GetProfileList();
    std::string                 prefix          = "";

    if(active_profile != "")
    {
        prefix = " ";
    }

    for(std::size_t profile_idx = 0; profile_idx < profile_list.size(); profile_idx++)
    {
        /*-------------------------------------------------*\
        | Print profile name                                |
        \*-------------------------------------------------*/
        if(profile_list[profile_idx] == active_profile)
        {
            std::cout << "*" << profile_idx << ": " << profile_list[profile_idx] << std::endl;
        }
        else
        {
            std::cout << prefix << profile_idx << ": " << profile_list[profile_idx] << std::endl;
        }
    }
}

bool OptionDevice(std::vector<DeviceOptions>* current_devices, std::string argument, Options* options, std::vector<RGBController *>& rgb_controllers)
{
    bool found = false;
    ResourceManager::get()->WaitForDetection();

    try
    {
        int current_device = std::stoi(argument);

        LOG_TRACE("[CLI] using device number %d for argument %s", current_device, argument.c_str());

        if((current_device >= static_cast<int>(rgb_controllers.size())) || (current_device < 0))
        {
            throw nullptr;
        }

        DeviceOptions newDev;
        newDev.device = current_device;

        if(!options->hasDevice)
        {
            options->hasDevice = true;
        }

        current_devices->push_back(newDev);

        found = true;
    }
    catch(...)
    {
        if(argument.length() > 1)
        {
            std::string argument_lower = argument;
            std::transform(argument_lower.begin(), argument_lower.end(), argument_lower.begin(), ::tolower);

            LOG_TRACE("[CLI] Searching for %s", argument_lower.c_str());

            for(unsigned int i = 0; i < rgb_controllers.size(); i++)
            {
                /*-----------------------------------------*\
                | If the argument is not a number then      |
                | check all the controllers names for a     |
                | match                                     |
                \*-----------------------------------------*/
                std::string name            = rgb_controllers[i]->GetDisplayName();
                std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                LOG_TRACE("[CLI] Comparing to %s", name.c_str());

                if(name.find(argument_lower) != std::string::npos)
                {
                    found                   = true;

                    DeviceOptions newDev;
                    newDev.device           = i;

                    if(!options->hasDevice)
                    {
                        options->hasDevice  = true;
                    }

                    current_devices->push_back(newDev);
                }
            }
        }
        else
        {
            std::cout << "Error: Empty device ID" << std::endl;
            return false;
        }
    }

    if(!found)
    {
        std::cout << "Error: Cannot find device \"" << argument << "\"" << std::endl;
    }

    return found;
}

bool OptionZone(std::vector<DeviceOptions>* current_devices, std::string argument, Options* /*options*/, std::vector<RGBController *>& rgb_controllers)
{
    bool found = false;
    ResourceManager::get()->WaitForDetection();

    try
    {
        int current_zone = std::stoi(argument);

        for(size_t i = 0; i < current_devices->size(); i++)
        {
            int current_device = current_devices->at(i).device;

            if(current_zone >= static_cast<int>(rgb_controllers[current_device]->GetZoneCount()) || (current_zone < 0))
            {
                throw nullptr;
            }

            current_devices->at(i).zone = current_zone;
            found = true;
        }
    }
    catch(...)
    {
        std::cout << "Error: Invalid zone ID: " + argument << std::endl;
        return false;
    }

    return found;
}

bool CheckColor(std::string argument, DeviceOptions* currentDevOpts)
{
    if(ParseColors(argument, currentDevOpts))
    {
        currentDevOpts->hasOption = true;
        return true;
    }
    else
    {
        std::cout << "Error: Invalid color value: " + argument << std::endl;
        return false;
    }
}

bool OptionColor(std::vector<DeviceOptions>* current_devices, std::string argument, Options* options)
{
    /*-----------------------------------------------------*\
    | If a device is not selected  i.e. size() == 0 then    |
    | add color to allDeviceOptions                         |
    \*-----------------------------------------------------*/
    bool found                      = false;
    DeviceOptions* currentDevOpts   = &options->allDeviceOptions;

    if(current_devices->size() == 0)
    {
        found = CheckColor(argument, currentDevOpts);
    }
    else
    {
        for(size_t i = 0; i < current_devices->size(); i++)
        {
            currentDevOpts = &current_devices->at(i);

            found = CheckColor(argument, currentDevOpts);
        }
    }

    return found;
}

bool OptionMode(std::vector<DeviceOptions>* current_devices, std::string argument, Options* options)
{
    if(argument.size() == 0)
    {
        std::cout << "Error: --mode passed with no argument" << std::endl;
        return false;
    }

    /*-----------------------------------------------------*\
    | If a device is not selected  i.e. size() == 0 then    |
    | add mode to allDeviceOptions                          |
    \*-----------------------------------------------------*/
    bool found                      = false;
    DeviceOptions* currentDevOpts   = &options->allDeviceOptions;

    if(current_devices->size() == 0)
    {
        currentDevOpts->mode = argument;
        currentDevOpts->hasOption = true;
        found = true;
    }
    else
    {
        for(size_t i = 0; i < current_devices->size(); i++)
        {
            currentDevOpts = &current_devices->at(i);

            currentDevOpts->mode = argument;
            currentDevOpts->hasOption = true;
            found = true;
        }
    }

    if(!found)
    {
        std::cout << "Error: No devices for mode \"" << argument << "\"" << std::endl;
    }
    return found;
}

bool OptionSpeed(std::vector<DeviceOptions>* current_devices, std::string argument, Options* options)
{
    if(argument.size() == 0)
    {
        std::cout << "Error: --speed passed with no argument" << std::endl;
        return false;
    }

    /*-----------------------------------------------------*\
    | If a device is not selected  i.e. size() == 0 then    |
    | add speed to allDeviceOptions                         |
    \*-----------------------------------------------------*/
    bool found                              = false;
    DeviceOptions* currentDevOpts           = &options->allDeviceOptions;

    if(current_devices->size() == 0)
    {
        currentDevOpts->speed               = std::min(std::max(std::stoi(argument), 0), 100);
        currentDevOpts->hasOption           = true;
        found = true;
    }
    else
    {
        for(size_t i = 0; i < current_devices->size(); i++)
        {
            DeviceOptions* currentDevOpts   = &current_devices->at(i);

            currentDevOpts->speed           = std::min(std::max(std::stoi(argument), 0), 100);
            currentDevOpts->hasOption       = true;
            found                           = true;
        }
    }

    if(!found)
    {
        std::cout << "Error: No devices for speed \"" << argument << "\"" << std::endl;
    }

    return found;
}

bool OptionBrightness(std::vector<DeviceOptions>* current_devices, std::string argument, Options* options)
{
    if(argument.size() == 0)
    {
        std::cout << "Error: --brightness passed with no argument" << std::endl;
        return false;
    }

    /*-----------------------------------------------------*\
    | If a device is not selected  i.e. size() == 0 then    |
    | add brightness to allDeviceOptions                    |
    \*-----------------------------------------------------*/
    bool found                      = false;
    DeviceOptions* currentDevOpts   = &options->allDeviceOptions;

    if(current_devices->size() == 0)
    {
        currentDevOpts->brightness  = std::min(std::max(std::stoi(argument), 0), 100);
        currentDevOpts->hasOption   = true;
        found = true;
    }
    else
    {
        for(size_t i = 0; i < current_devices->size(); i++)
        {
            DeviceOptions* currentDevOpts   = &current_devices->at(i);

            currentDevOpts->brightness      = std::min(std::max(std::stoi(argument), 0), 100);
            currentDevOpts->hasOption       = true;
            found = true;
        }
    }

    if(!found)
    {
        std::cout << "Error: No devices for brightness \"" << argument << "\"" << std::endl;
    }
    return found;
}

bool OptionSize(std::vector<DeviceOptions>* current_devices, std::string argument, Options* /*options*/, std::vector<RGBController *>& rgb_controllers)
{
    const unsigned int new_size = std::stoi(argument);

    ResourceManager::get()->WaitForDetection();

    for(size_t i = 0; i < current_devices->size(); i++)
    {
        int current_device      = current_devices->at(i).device;
        int current_zone        = current_devices->at(i).zone;

        /*-------------------------------------------------*\
        | Fail out if device, zone, or size are out of      |
        | range                                             |
        \*-------------------------------------------------*/
        if((current_device >= static_cast<int>(rgb_controllers.size())) || (current_device < 0))
        {
            std::cout << "Error: Device is out of range" << std::endl;
            return false;
        }
        else if((current_zone >= static_cast<int>(rgb_controllers[current_device]->GetZoneCount())) || (current_zone < 0))
        {
            std::cout << "Error: Zone is out of range" << std::endl;
            return false;
        }
        else if((new_size < rgb_controllers[current_device]->GetZoneLEDsMin(current_zone)) || (new_size > rgb_controllers[current_device]->GetZoneLEDsMax(current_zone)))
        {
            std::cout << "Error: New size is out of range" << std::endl;
        }

        /*-------------------------------------------------*\
        | Resize the zone                                   |
        \*-------------------------------------------------*/
        rgb_controllers[current_device]->ResizeZone(current_zone, new_size);
        current_devices->at(i).hasConfig = true;

        /*-------------------------------------------------*\
        | Save configuration                                |
        \*-------------------------------------------------*/
        ResourceManager::get()->GetProfileManager()->SaveConfiguration();
    }

    return true;
}

bool OptionProfile(std::string argument)
{
    ResourceManager::get()->WaitForDetection();

    /*-----------------------------------------------------*\
    | Attempt to load profile                               |
    \*-----------------------------------------------------*/
    if(ResourceManager::get()->GetProfileManager()->LoadProfile(argument))
    {
        std::cout << "Profile loaded successfully" << std::endl;
        return true;
    }
    else
    {
        std::cout << "Profile failed to load" << std::endl;
        return false;
    }
}

bool OptionSaveProfile(std::string argument)
{
    /*-----------------------------------------------------*\
    | Set save profile name                                 |
    \*-----------------------------------------------------*/
    profile_save_name = argument;

    return(true);
}

/*---------------------------------------------------------*\
| Extended CLI options                                      |
|                                                           |
|   These options expose functions that were previously     |
|   only available from the GUI: mode direction, per-LED    |
|   selection, saving to device memory, device/zone names,  |
|   zone segments, rescanning, profile management, automatic|
|   profiles, detector selection and general settings.      |
\*---------------------------------------------------------*/

/*---------------------------------------------------------*\
| Direction names accepted by --direction                   |
\*---------------------------------------------------------*/
struct DirectionName { int value; const char* keyword; } static const direction_names[] =
{
    { MODE_DIRECTION_LEFT,          "left"          },
    { MODE_DIRECTION_RIGHT,         "right"         },
    { MODE_DIRECTION_UP,            "up"            },
    { MODE_DIRECTION_DOWN,          "down"          },
    { MODE_DIRECTION_HORIZONTAL,    "horizontal"    },
    { MODE_DIRECTION_VERTICAL,      "vertical"      },
    { MODE_DIRECTION_UP_LEFT,       "up-left"       },
    { MODE_DIRECTION_UP_RIGHT,      "up-right"      },
    { MODE_DIRECTION_DOWN_LEFT,     "down-left"     },
    { MODE_DIRECTION_DOWN_RIGHT,    "down-right"    },
};

/*---------------------------------------------------------*\
| Automatic profile events accepted by --auto-profile       |
\*---------------------------------------------------------*/
struct AutoProfileEvent { const char* keyword; const char* setting; } static const auto_profile_events[] =
{
    { "service-startup",    "service_startup_profile"   },
    { "service-shutdown",   "service_shutdown_profile"  },
    { "open",               "open_profile"              },
    { "exit",               "exit_profile"              },
    { "suspend",            "suspend_profile"           },
    { "resume",             "resume_profile"            },
};

static const char* DirectionToString(int direction)
{
    for(std::size_t idx = 0; idx < (sizeof(direction_names) / sizeof(direction_names[0])); idx++)
    {
        if(direction_names[idx].value == direction)
        {
            return(direction_names[idx].keyword);
        }
    }

    return("unknown");
}

static bool DirectionSupported(unsigned int mode_flags, int direction)
{
    switch(direction)
    {
        case MODE_DIRECTION_LEFT:
        case MODE_DIRECTION_RIGHT:
            return((mode_flags & MODE_FLAG_HAS_DIRECTION_LR) != 0);

        case MODE_DIRECTION_UP:
        case MODE_DIRECTION_DOWN:
            return((mode_flags & MODE_FLAG_HAS_DIRECTION_UD) != 0);

        case MODE_DIRECTION_HORIZONTAL:
        case MODE_DIRECTION_VERTICAL:
            return((mode_flags & MODE_FLAG_HAS_DIRECTION_HV) != 0);

        case MODE_DIRECTION_UP_LEFT:
        case MODE_DIRECTION_UP_RIGHT:
        case MODE_DIRECTION_DOWN_LEFT:
        case MODE_DIRECTION_DOWN_RIGHT:
            return((mode_flags & MODE_FLAG_HAS_DIRECTION_DIAG) != 0);
    }

    return(false);
}

static std::string ModeCapabilities(RGBController* controller, unsigned int mode_idx)
{
    unsigned int    mode_flags  = controller->GetModeFlags(mode_idx);
    std::string     caps;

    if(mode_flags & MODE_FLAG_HAS_SPEED)
    {
        caps += " speed";
    }

    if(mode_flags & MODE_FLAG_HAS_BRIGHTNESS)
    {
        caps += " brightness";
    }

    if(mode_flags & MODE_FLAG_HAS_DIRECTION_LR)
    {
        caps += " direction(left/right)";
    }

    if(mode_flags & MODE_FLAG_HAS_DIRECTION_UD)
    {
        caps += " direction(up/down)";
    }

    if(mode_flags & MODE_FLAG_HAS_DIRECTION_HV)
    {
        caps += " direction(horizontal/vertical)";
    }

    if(mode_flags & MODE_FLAG_HAS_DIRECTION_DIAG)
    {
        caps += " direction(diagonal)";
    }

    if(mode_flags & MODE_FLAG_HAS_PER_LED_COLOR)
    {
        caps += " per-led-colors";
    }

    if(mode_flags & MODE_FLAG_HAS_MODE_SPECIFIC_COLOR)
    {
        caps += " mode-colors(" + std::to_string(controller->GetModeColorsMin(mode_idx)) + "-" + std::to_string(controller->GetModeColorsMax(mode_idx)) + ")";
    }

    if(mode_flags & MODE_FLAG_HAS_RANDOM_COLOR)
    {
        caps += " random-color";
    }

    if(mode_flags & MODE_FLAG_MANUAL_SAVE)
    {
        caps += " save-to-device";
    }

    if(mode_flags & MODE_FLAG_AUTOMATIC_SAVE)
    {
        caps += " saves-automatically";
    }

    return(caps);
}

void OptionListDevicesExtended(std::vector<RGBController *>& rgb_controllers)
{
    ResourceManager::get()->WaitForDetection();

    for(std::size_t controller_idx = 0; controller_idx < rgb_controllers.size(); controller_idx++)
    {
        RGBController* controller = rgb_controllers[controller_idx];

        std::cout << controller_idx << ": " << controller->GetDisplayName() << std::endl;

        if(controller->GetFlags() & CONTROLLER_FLAG_MANUALLY_CONFIGURED_NAME)
        {
            std::cout << "  Original name:  " << controller->GetName() << std::endl;
        }

        std::cout << "  Type:           " << RGBController::DeviceTypeToString(controller->GetDeviceType()) << std::endl;
        std::cout << "  Renamable:      " << ((controller->GetFlags() & CONTROLLER_FLAG_MANUALLY_CONFIGURABLE_NAME) ? "yes" : "no") << std::endl;

        /*-------------------------------------------------*\
        | Modes with their capabilities                     |
        \*-------------------------------------------------*/
        std::cout << "  Modes:" << std::endl;

        for(unsigned int mode_idx = 0; mode_idx < controller->GetModeCount(); mode_idx++)
        {
            std::cout << "    " << (((int)mode_idx == controller->GetActiveMode()) ? "* " : "  ")
                      << QuoteIfNecessary(controller->GetModeName(mode_idx)) << ":" << ModeCapabilities(controller, mode_idx);

            if(controller->GetModeFlags(mode_idx) & (MODE_FLAG_HAS_DIRECTION_LR | MODE_FLAG_HAS_DIRECTION_UD | MODE_FLAG_HAS_DIRECTION_HV | MODE_FLAG_HAS_DIRECTION_DIAG))
            {
                std::cout << " [current direction: " << DirectionToString((int)controller->GetModeDirection(mode_idx)) << "]";
            }

            std::cout << std::endl;
        }

        /*-------------------------------------------------*\
        | Zones with LED ranges and segments                |
        \*-------------------------------------------------*/
        std::cout << "  Zones:" << std::endl;

        for(unsigned int zone_idx = 0; zone_idx < controller->GetZoneCount(); zone_idx++)
        {
            unsigned int zone_flags = controller->GetZoneFlags(zone_idx);
            unsigned int zone_start = controller->GetZoneStartIndex(zone_idx);
            unsigned int zone_leds  = controller->GetZoneLEDsCount(zone_idx);

            std::cout << "    " << zone_idx << ": " << QuoteIfNecessary(controller->GetZoneDisplayName(zone_idx))
                      << "  LEDs " << zone_leds;

            if(zone_leds > 0)
            {
                std::cout << " (device LEDs " << zone_start << "-" << (zone_start + zone_leds - 1) << ")";
            }

            if(zone_flags & (ZONE_FLAG_MANUALLY_CONFIGURABLE_SIZE | ZONE_FLAG_MANUALLY_CONFIGURABLE_SIZE_EFFECTS_ONLY))
            {
                std::cout << "  resizable " << controller->GetZoneLEDsMin(zone_idx) << "-" << controller->GetZoneLEDsMax(zone_idx);
            }

            if(zone_flags & ZONE_FLAG_MANUALLY_CONFIGURABLE_NAME)
            {
                std::cout << "  renamable";
            }

            if(zone_flags & ZONE_FLAG_MANUALLY_CONFIGURABLE_SEGMENTS)
            {
                std::cout << "  segments-editable";
            }

            std::cout << std::endl;

            for(unsigned int segment_idx = 0; segment_idx < controller->GetZoneSegmentCount(zone_idx); segment_idx++)
            {
                unsigned int segment_start = controller->GetZoneSegmentStartIndex(zone_idx, segment_idx);
                unsigned int segment_leds  = controller->GetZoneSegmentLEDsCount(zone_idx, segment_idx);

                std::cout << "         segment " << segment_idx << ": " << QuoteIfNecessary(controller->GetZoneSegmentName(zone_idx, segment_idx))
                          << "  LEDs " << segment_leds;

                if(segment_leds > 0)
                {
                    std::cout << " (zone LEDs " << segment_start << "-" << (segment_start + segment_leds - 1) << ")";
                }

                std::cout << std::endl;
            }
        }

        std::cout << std::endl;
    }
}

bool OptionDirection(std::vector<DeviceOptions>* current_devices, std::string argument, Options* options)
{
    int direction = -1;

    for(std::size_t idx = 0; idx < (sizeof(direction_names) / sizeof(direction_names[0])); idx++)
    {
        if(strcasecmp(argument.c_str(), direction_names[idx].keyword) == 0)
        {
            direction = direction_names[idx].value;
            break;
        }
    }

    if(direction < 0)
    {
        std::cout << "Error: Invalid direction \"" << argument << "\"" << std::endl;
        return(false);
    }

    if(current_devices->size() == 0)
    {
        options->allDeviceOptions.direction = direction;
        options->allDeviceOptions.hasOption = true;
    }
    else
    {
        for(std::size_t i = 0; i < current_devices->size(); i++)
        {
            current_devices->at(i).direction = direction;
            current_devices->at(i).hasOption = true;
        }
    }

    return(true);
}

static bool ParseLedList(std::string argument, std::vector<unsigned int>& leds)
{
    std::size_t pos = 0;

    leds.clear();

    while(pos <= argument.size())
    {
        std::size_t comma = argument.find(',', pos);
        std::string item  = argument.substr(pos, (comma == std::string::npos) ? std::string::npos : comma - pos);

        if(item.empty())
        {
            return(false);
        }

        try
        {
            std::size_t dash = item.find('-');

            if(dash == std::string::npos)
            {
                leds.push_back((unsigned int)std::stoul(item));
            }
            else
            {
                unsigned int first = (unsigned int)std::stoul(item.substr(0, dash));
                unsigned int last  = (unsigned int)std::stoul(item.substr(dash + 1));

                if(last < first)
                {
                    return(false);
                }

                for(unsigned int led_idx = first; led_idx <= last; led_idx++)
                {
                    leds.push_back(led_idx);
                }
            }
        }
        catch(...)
        {
            return(false);
        }

        if(comma == std::string::npos)
        {
            break;
        }

        pos = comma + 1;
    }

    return(leds.size() > 0);
}

bool OptionLed(std::vector<DeviceOptions>* current_devices, std::string argument)
{
    if(current_devices->size() == 0)
    {
        std::cout << "Error: --led must be specified after --device" << std::endl;
        return(false);
    }

    std::vector<unsigned int> leds;

    if(!ParseLedList(argument, leds))
    {
        std::cout << "Error: Invalid LED list \"" << argument << "\" (example: 0,3,5-9)" << std::endl;
        return(false);
    }

    for(std::size_t i = 0; i < current_devices->size(); i++)
    {
        current_devices->at(i).leds = leds;
    }

    return(true);
}

bool OptionSaveToDevice(std::vector<DeviceOptions>* current_devices, Options* options)
{
    if(current_devices->size() == 0)
    {
        options->allDeviceOptions.save_to_device = true;
        options->allDeviceOptions.hasOption      = true;
    }
    else
    {
        for(std::size_t i = 0; i < current_devices->size(); i++)
        {
            current_devices->at(i).save_to_device = true;
            current_devices->at(i).hasOption      = true;
        }
    }

    return(true);
}

bool OptionRenameDevice(std::vector<DeviceOptions>* current_devices, std::string argument, std::vector<RGBController *>& rgb_controllers)
{
    if(current_devices->size() != 1)
    {
        std::cout << "Error: --rename-device needs exactly one device selected with --device" << std::endl;
        return(false);
    }

    RGBController* controller = rgb_controllers[current_devices->at(0).device];

    if(!(controller->GetFlags() & CONTROLLER_FLAG_MANUALLY_CONFIGURABLE_NAME))
    {
        std::cout << "Error: Device \"" << controller->GetDisplayName() << "\" cannot be renamed" << std::endl;
        return(false);
    }

    controller_flags new_flags = controller->GetFlags();

    if(argument.empty())
    {
        new_flags = (controller_flags)(new_flags & ~CONTROLLER_FLAG_MANUALLY_CONFIGURED_NAME);
    }
    else
    {
        new_flags = (controller_flags)(new_flags | CONTROLLER_FLAG_MANUALLY_CONFIGURED_NAME);
    }

    controller->ConfigureDevice(new_flags, argument);
    ResourceManager::get()->GetProfileManager()->SaveConfiguration();

    std::cout << "Device " << current_devices->at(0).device << " is now named \"" << controller->GetDisplayName() << "\"" << std::endl;

    current_devices->at(0).hasConfig = true;

    return(true);
}

static bool GetSelectedZone(std::vector<DeviceOptions>* current_devices, std::vector<RGBController *>& rgb_controllers, const char* option_name, RGBController** controller, int* zone_idx)
{
    if(current_devices->size() != 1 || current_devices->at(0).zone < 0)
    {
        std::cout << "Error: " << option_name << " needs exactly one device and one zone selected with --device and --zone" << std::endl;
        return(false);
    }

    *controller = rgb_controllers[current_devices->at(0).device];
    *zone_idx   = current_devices->at(0).zone;

    return(true);
}

bool OptionRenameZone(std::vector<DeviceOptions>* current_devices, std::string argument, std::vector<RGBController *>& rgb_controllers)
{
    RGBController*  controller;
    int             zone_idx;

    if(!GetSelectedZone(current_devices, rgb_controllers, "--rename-zone", &controller, &zone_idx))
    {
        return(false);
    }

    if(!(controller->GetZoneFlags(zone_idx) & ZONE_FLAG_MANUALLY_CONFIGURABLE_NAME))
    {
        std::cout << "Error: Zone \"" << controller->GetZoneDisplayName(zone_idx) << "\" cannot be renamed" << std::endl;
        return(false);
    }

    zone new_zone = controller->GetZone((unsigned int)zone_idx);

    if(argument.empty())
    {
        new_zone.flags = (zone_flags)(new_zone.flags & ~ZONE_FLAG_MANUALLY_CONFIGURED_NAME);
    }
    else
    {
        new_zone.flags        = (zone_flags)(new_zone.flags | ZONE_FLAG_MANUALLY_CONFIGURED_NAME);
        new_zone.display_name = argument;
    }

    controller->ConfigureZone(zone_idx, new_zone);
    ResourceManager::get()->GetProfileManager()->SaveConfiguration();

    std::cout << "Zone " << zone_idx << " is now named \"" << controller->GetZoneDisplayName(zone_idx) << "\"" << std::endl;

    current_devices->at(0).hasConfig = true;

    return(true);
}

bool OptionSegments(std::vector<DeviceOptions>* current_devices, std::string argument, std::vector<RGBController *>& rgb_controllers)
{
    RGBController*  controller;
    int             zone_idx;

    if(!GetSelectedZone(current_devices, rgb_controllers, "--segments", &controller, &zone_idx))
    {
        return(false);
    }

    if(!(controller->GetZoneFlags(zone_idx) & ZONE_FLAG_MANUALLY_CONFIGURABLE_SEGMENTS))
    {
        std::cout << "Error: Segments of zone \"" << controller->GetZoneDisplayName(zone_idx) << "\" cannot be edited" << std::endl;
        return(false);
    }

    zone            new_zone    = controller->GetZone((unsigned int)zone_idx);
    unsigned int    start_idx   = 0;

    new_zone.segments.clear();

    if(argument.empty())
    {
        /*-------------------------------------------------*\
        | Empty argument restores the default segments      |
        \*-------------------------------------------------*/
        new_zone.flags = (zone_flags)(new_zone.flags & ~ZONE_FLAG_MANUALLY_CONFIGURED_SEGMENTS);
    }
    else
    {
        std::size_t pos = 0;

        while(pos <= argument.size())
        {
            std::size_t comma = argument.find(',', pos);
            std::string item  = argument.substr(pos, (comma == std::string::npos) ? std::string::npos : comma - pos);
            std::size_t colon = item.rfind(':');

            if(colon == std::string::npos || colon == 0)
            {
                std::cout << "Error: Invalid segment \"" << item << "\" (expected name:size)" << std::endl;
                return(false);
            }

            unsigned int leds_count;

            try
            {
                leds_count = (unsigned int)std::stoul(item.substr(colon + 1));
            }
            catch(...)
            {
                std::cout << "Error: Invalid segment size in \"" << item << "\"" << std::endl;
                return(false);
            }

            segment new_segment;

            new_segment.name        = item.substr(0, colon);
            new_segment.type        = new_zone.type;
            new_segment.start_idx   = start_idx;
            new_segment.leds_count  = leds_count;
            new_segment.flags       = 0;

            new_zone.segments.push_back(new_segment);

            start_idx += leds_count;

            if(comma == std::string::npos)
            {
                break;
            }

            pos = comma + 1;
        }

        if(start_idx > new_zone.leds_count)
        {
            std::cout << "Error: Segments use " << start_idx << " LEDs but the zone only has " << new_zone.leds_count << std::endl;
            return(false);
        }

        new_zone.flags = (zone_flags)(new_zone.flags | ZONE_FLAG_MANUALLY_CONFIGURED_SEGMENTS);
    }

    controller->ConfigureZone(zone_idx, new_zone);
    ResourceManager::get()->GetProfileManager()->SaveConfiguration();

    std::cout << "Zone " << zone_idx << " now has " << controller->GetZoneSegmentCount(zone_idx) << " segment(s)" << std::endl;

    current_devices->at(0).hasConfig = true;

    return(true);
}

void OptionRescan()
{
    std::cout << "Rescanning devices..." << std::endl;

    NetworkClient* local_client = ResourceManager::get()->IsLocalClient() ? ResourceManager::get()->GetLocalClient() : NULL;

    ResourceManager::get()->RescanDevices();

    if(local_client != NULL && local_client->GetSupportsDetectionAPI())
    {
        /*-------------------------------------------------*\
        | Rescan runs in the service; wait for it to start  |
        | and then to reach 100%                            |
        \*-------------------------------------------------*/
        bool started = false;

        for(int waited_ms = 0; waited_ms < 60000; waited_ms += 100)
        {
            unsigned int percent = local_client->DetectionManager_GetDetectionPercent();

            if(percent < 100)
            {
                started = true;
            }
            else if(started || waited_ms >= 3000)
            {
                break;
            }

            std::this_thread::sleep_for(100ms);
        }
    }
    else
    {
        ResourceManager::get()->WaitForDetection();
    }

    std::cout << "Rescan complete" << std::endl;
}

bool OptionDeleteProfile(std::string argument)
{
    std::vector<std::string> profile_list = ResourceManager::get()->GetProfileManager()->GetProfileList();

    if(std::find(profile_list.begin(), profile_list.end(), argument) == profile_list.end())
    {
        std::cout << "Error: Profile \"" << argument << "\" does not exist" << std::endl;
        return(false);
    }

    ResourceManager::get()->GetProfileManager()->DeleteProfile(argument);

    std::cout << "Profile \"" << argument << "\" deleted" << std::endl;

    return(true);
}

bool OptionRenameProfile(std::string old_name, std::string new_name)
{
    ProfileManager*             profile_manager = ResourceManager::get()->GetProfileManager();
    std::vector<std::string>    profile_list    = profile_manager->GetProfileList();

    if(new_name.empty())
    {
        std::cout << "Error: --rename-profile needs the old and the new name" << std::endl;
        return(false);
    }

    if(std::find(profile_list.begin(), profile_list.end(), old_name) == profile_list.end())
    {
        std::cout << "Error: Profile \"" << old_name << "\" does not exist" << std::endl;
        return(false);
    }

    if(std::find(profile_list.begin(), profile_list.end(), new_name) != profile_list.end())
    {
        std::cout << "Error: Profile \"" << new_name << "\" already exists" << std::endl;
        return(false);
    }

    nlohmann::json profile_json = profile_manager->ReadProfileJSON(old_name);

    if(profile_json.is_null() || !profile_json.contains("profile_name"))
    {
        std::cout << "Error: Could not read profile \"" << old_name << "\"" << std::endl;
        return(false);
    }

    profile_json["profile_name"] = new_name;

    /*-----------------------------------------------------*\
    | Write the copy through the service when connected to  |
    | one, so it updates its own profile list               |
    \*-----------------------------------------------------*/
    if(ResourceManager::get()->IsLocalClient() && ResourceManager::get()->GetLocalClient()->GetSupportsProfileManagerAPI())
    {
        ResourceManager::get()->GetLocalClient()->ProfileManager_UploadProfile(profile_json.dump());
    }
    else if(!profile_manager->SaveProfileFromJSON(profile_json))
    {
        std::cout << "Error: Could not save profile \"" << new_name << "\"" << std::endl;
        return(false);
    }

    profile_manager->DeleteProfile(old_name);

    /*-----------------------------------------------------*\
    | Keep automatic profiles pointing at the new name      |
    \*-----------------------------------------------------*/
    SettingsManager*    settings_manager = ResourceManager::get()->GetSettingsManager();
    nlohmann::json      auto_settings    = settings_manager->GetSettings("ProfileManager");
    bool                auto_changed     = false;

    for(std::size_t idx = 0; idx < (sizeof(auto_profile_events) / sizeof(auto_profile_events[0])); idx++)
    {
        const char* setting = auto_profile_events[idx].setting;

        if(auto_settings.contains(setting) && auto_settings[setting].contains("name") && auto_settings[setting]["name"] == old_name)
        {
            auto_settings[setting]["name"] = new_name;
            auto_changed                   = true;
        }
    }

    if(auto_changed)
    {
        settings_manager->SetSettings("ProfileManager", auto_settings);
        settings_manager->SaveSettings();
    }

    std::cout << "Profile \"" << old_name << "\" renamed to \"" << new_name << "\"" << std::endl;

    return(true);
}

void OptionListAutoProfiles()
{
    nlohmann::json auto_settings = ResourceManager::get()->GetSettingsManager()->GetSettings("ProfileManager");

    for(std::size_t idx = 0; idx < (sizeof(auto_profile_events) / sizeof(auto_profile_events[0])); idx++)
    {
        const char* setting = auto_profile_events[idx].setting;
        std::string value   = "off";

        if(auto_settings.contains(setting)
        && auto_settings[setting].contains("enabled") && auto_settings[setting]["enabled"] == true
        && auto_settings[setting].contains("name"))
        {
            value = "\"" + auto_settings[setting]["name"].get<std::string>() + "\"";
        }

        std::cout << std::left << std::setw(18) << auto_profile_events[idx].keyword << value << std::endl;
    }
}

bool OptionAutoProfile(std::string event, std::string profile_name)
{
    const char* setting = NULL;

    for(std::size_t idx = 0; idx < (sizeof(auto_profile_events) / sizeof(auto_profile_events[0])); idx++)
    {
        if(strcasecmp(event.c_str(), auto_profile_events[idx].keyword) == 0)
        {
            setting = auto_profile_events[idx].setting;
            break;
        }
    }

    if(setting == NULL)
    {
        std::cout << "Error: Invalid event \"" << event << "\". Use service-startup, service-shutdown, open, exit, suspend or resume" << std::endl;
        return(false);
    }

    if(profile_name.empty())
    {
        std::cout << "Error: --auto-profile needs an event and a profile name (or off)" << std::endl;
        return(false);
    }

    nlohmann::json entry;

    if(strcasecmp(profile_name.c_str(), "off") == 0)
    {
        entry["enabled"] = false;
        entry["name"]    = "";
    }
    else
    {
        std::vector<std::string> profile_list = ResourceManager::get()->GetProfileManager()->GetProfileList();

        if(std::find(profile_list.begin(), profile_list.end(), profile_name) == profile_list.end())
        {
            std::cout << "Error: Profile \"" << profile_name << "\" does not exist" << std::endl;
            return(false);
        }

        entry["enabled"] = true;
        entry["name"]    = profile_name;
    }

    nlohmann::json change;
    change[setting] = entry;

    SettingsManager* settings_manager = ResourceManager::get()->GetSettingsManager();

    settings_manager->ModifySettings("ProfileManager", change);
    settings_manager->SaveSettings();

    std::cout << "Automatic profile for " << event << ": " << (entry["enabled"] == true ? "\"" + profile_name + "\"" : std::string("off")) << std::endl;

    return(true);
}

static std::string ToLower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), ::tolower);
    return(text);
}

void OptionListDetectors(std::string filter)
{
    nlohmann::json  detector_settings   = ResourceManager::get()->GetSettingsManager()->GetSettings("Detectors");
    std::string     filter_lower        = ToLower(filter);
    unsigned int    shown               = 0;

    if(!detector_settings.contains("detectors"))
    {
        std::cout << "No detector settings found" << std::endl;
        return;
    }

    for(auto& element : detector_settings["detectors"].items())
    {
        if(!filter_lower.empty() && ToLower(element.key()).find(filter_lower) == std::string::npos)
        {
            continue;
        }

        std::cout << (element.value() == true ? "[enabled]  " : "[disabled] ") << element.key() << std::endl;
        shown++;
    }

    std::cout << shown << " detector(s)" << std::endl;
}

bool OptionSetDetector(std::string name, bool enabled)
{
    SettingsManager*    settings_manager    = ResourceManager::get()->GetSettingsManager();
    nlohmann::json      detector_settings   = settings_manager->GetSettings("Detectors");

    if(!detector_settings.contains("detectors"))
    {
        std::cout << "Error: No detector settings found" << std::endl;
        return(false);
    }

    /*-----------------------------------------------------*\
    | Exact (case-insensitive) match first, then a unique   |
    | partial match                                         |
    \*-----------------------------------------------------*/
    std::string                 name_lower  = ToLower(name);
    std::string                 match;
    std::vector<std::string>    partial;

    for(auto& element : detector_settings["detectors"].items())
    {
        std::string key_lower = ToLower(element.key());

        if(key_lower == name_lower)
        {
            match = element.key();
            break;
        }

        if(key_lower.find(name_lower) != std::string::npos)
        {
            partial.push_back(element.key());
        }
    }

    if(match.empty())
    {
        if(partial.size() == 1)
        {
            match = partial[0];
        }
        else if(partial.size() == 0)
        {
            std::cout << "Error: No detector matches \"" << name << "\". Use --list-detectors to see the names" << std::endl;
            return(false);
        }
        else
        {
            std::cout << "Error: \"" << name << "\" matches " << partial.size() << " detectors, be more specific:" << std::endl;

            for(std::size_t idx = 0; idx < partial.size(); idx++)
            {
                std::cout << "  " << partial[idx] << std::endl;
            }

            return(false);
        }
    }

    nlohmann::json change;
    change["detectors"][match] = enabled;

    settings_manager->ModifySettings("Detectors", change);
    settings_manager->SaveSettings();

    std::cout << "Detector \"" << match << "\" " << (enabled ? "enabled" : "disabled") << ". Rescan (--rescan) or restart the service to apply" << std::endl;

    return(true);
}

void OptionListSettings()
{
    nlohmann::json schema = ResourceManager::get()->GetSettingsManager()->GetSettingsSchema("");

    for(auto& section : schema.items())
    {
        std::string title = section.value().contains("title") ? section.value()["title"].get<std::string>() : "";

        std::cout << section.key() << "  (" << title << ")" << std::endl;

        if(!section.value().contains("properties"))
        {
            continue;
        }

        for(auto& property : section.value()["properties"].items())
        {
            std::string type        = property.value().contains("type")        ? property.value()["type"].dump()                    : "";
            std::string description = property.value().contains("description") ? property.value()["description"].get<std::string>() : "";

            std::cout << "    " << section.key() << "." << property.key() << "  " << type;

            if(!description.empty())
            {
                std::cout << "  - " << description;
            }

            std::cout << std::endl;
        }
    }
}

bool OptionGetSetting(std::string argument)
{
    std::size_t     dot         = argument.find('.');
    std::string     section     = argument.substr(0, dot);
    nlohmann::json  settings    = ResourceManager::get()->GetSettingsManager()->GetSettings(section);

    if(dot == std::string::npos)
    {
        if(settings.is_null())
        {
            std::cout << "{}  (nothing stored for " << section << ", defaults are in use)" << std::endl;
        }
        else
        {
            std::cout << settings.dump(4) << std::endl;
        }

        return(true);
    }

    std::string property = argument.substr(dot + 1);

    if(!settings.contains(property))
    {
        std::cout << "Error: Setting \"" << argument << "\" is not set. Use --list-settings to see the options" << std::endl;
        return(false);
    }

    std::cout << settings[property].dump(4) << std::endl;

    return(true);
}

bool OptionSetSetting(std::string argument, std::string value)
{
    std::size_t dot = argument.find('.');

    if(dot == std::string::npos || dot == 0 || dot == argument.size() - 1)
    {
        std::cout << "Error: --set-setting needs section.option, for example LogManager.log_console" << std::endl;
        return(false);
    }

    std::string     section     = argument.substr(0, dot);
    std::string     property    = argument.substr(dot + 1);
    nlohmann::json  new_value   = nlohmann::json::parse(value, nullptr, false);

    /*-----------------------------------------------------*\
    | Anything that is not valid JSON is stored as text     |
    \*-----------------------------------------------------*/
    if(new_value.is_discarded())
    {
        new_value = value;
    }

    nlohmann::json  schema  = ResourceManager::get()->GetSettingsManager()->GetSettingsSchema(section);

    if(!schema.is_null() && !schema.contains(property))
    {
        std::cout << "Error: \"" << property << "\" is not an option of " << section << ". Use --list-settings to see the options" << std::endl;
        return(false);
    }

    nlohmann::json change;
    change[property] = new_value;

    SettingsManager* settings_manager = ResourceManager::get()->GetSettingsManager();

    settings_manager->ModifySettings(section, change);
    settings_manager->SaveSettings();

    nlohmann::json result = settings_manager->GetSettings(section);

    if(result.contains(property))
    {
        std::cout << argument << " = " << result[property].dump() << std::endl;
        return(true);
    }

    std::cout << "Error: \"" << argument << "\" was not stored" << std::endl;
    return(false);
}

#ifdef __linux__
void OptionGenerateUdevRules(std::string argument)
{
    DetectionManager::get()->GenerateUdevRules(argument);
}

void OptionPrintUdevRules()
{
    DetectionManager::get()->PrintUdevRules();
}
#endif

int ProcessOptions(Options* options, std::vector<RGBController *>& rgb_controllers)
{
    unsigned int            ret_flags   = 0;
    int                     arg_index   = 1;
    std::vector<DeviceOptions> current_devices;

    options->hasDevice                  = false;
    options->profile_loaded             = false;

#ifdef _WIN32
    int                     fake_argc;
    wchar_t**               argvw       = CommandLineToArgvW(GetCommandLineW(), &fake_argc);
#endif

    while(arg_index < preserve_argc)
    {
        std::string         option      = preserve_argv[arg_index];
        std::string         argument    = "";
        filesystem::path    arg_path;

        /*-------------------------------------------------*\
        | Handle options that take an argument              |
        \*-------------------------------------------------*/
        if(arg_index + 1 < preserve_argc)
        {
            argument                    = preserve_argv[arg_index + 1];
#ifdef _WIN32
            arg_path                    = argvw[arg_index + 1];
#else
            arg_path                    = argument;
#endif
        }

        /*-------------------------------------------------*\
        | -l / --list-devices (no arguments)                |
        \*-------------------------------------------------*/
        if(option == "--list-devices" || option == "-l")
        {
            OptionListDevices(rgb_controllers, false);
            exit(0);
        }

        /*-------------------------------------------------*\
        | -ld / --list-detailed (no arguments)              |
        \*-------------------------------------------------*/
        else if(option == "--list-detailed" || option == "-ld")
        {
            OptionListDevices(rgb_controllers, true);
            exit(0);
        }

        /*-------------------------------------------------*\
        | -lp / --list-profiles (no arguments)              |
        \*-------------------------------------------------*/
        else if(option == "--list-profiles" || option == "-lp")
        {
            OptionListProfiles();
            exit(0);
        }

        /*-------------------------------------------------*\
        | -d / --device                                     |
        \*-------------------------------------------------*/
        else if(option == "--device" || option == "-d")
        {
            while(!current_devices.empty())
            {
                options->devices.push_back(current_devices.back());
                current_devices.pop_back();
            }

            if(!OptionDevice(&current_devices, argument, options, rgb_controllers))
            {
                return RET_FLAG_PRINT_HELP;
            }

            arg_index++;
        }

        /*-------------------------------------------------*\
        | -z / --zone                                       |
        \*-------------------------------------------------*/
        else if(option == "--zone" || option == "-z")
        {
            if(!OptionZone(&current_devices, argument, options, rgb_controllers))
            {
                return RET_FLAG_PRINT_HELP;
            }

            arg_index++;
        }

        /*-------------------------------------------------*\
        | -c / --color                                      |
        \*-------------------------------------------------*/
        else if(option == "--color" || option == "-c")
        {
            if(!OptionColor(&current_devices, argument, options))
            {
                return RET_FLAG_PRINT_HELP;
            }

            arg_index++;
        }

        /*-------------------------------------------------*\
        | -m / --mode                                       |
        \*-------------------------------------------------*/
        else if(option == "--mode" || option == "-m")
        {
            if(!OptionMode(&current_devices, argument, options))
            {
                return RET_FLAG_PRINT_HELP;
            }

            arg_index++;
        }

        /*-------------------------------------------------*\
        | -b / --brightness                                 |
        \*-------------------------------------------------*/
        else if(option == "--brightness" || option == "-b")
        {
            if(!OptionBrightness(&current_devices, argument, options))
            {
                return RET_FLAG_PRINT_HELP;
            }

            arg_index++;
        }

        /*-------------------------------------------------*\
        | -s / --speed                                      |
        \*-------------------------------------------------*/
        else if(option == "--speed" || option == "-s")
        {
            if(!OptionSpeed(&current_devices, argument, options))
            {
                return RET_FLAG_PRINT_HELP;
            }

            arg_index++;
        }

        /*-------------------------------------------------*\
        | -sz / --size                                      |
        \*-------------------------------------------------*/
        else if(option == "--size" || option == "-sz")
        {
            if(!OptionSize(&current_devices, argument, options, rgb_controllers))
            {
                return RET_FLAG_PRINT_HELP;
            }

            arg_index++;
        }

        /*-------------------------------------------------*\
        | -p / --profile                                    |
        \*-------------------------------------------------*/
        else if(option == "--profile" || option == "-p")
        {
            options->profile_loaded = OptionProfile(arg_path.generic_u8string());

            arg_index++;
        }

        /*-------------------------------------------------*\
        | -sp / --save-profile                              |
        \*-------------------------------------------------*/
        else if(option == "--save-profile" || option == "-sp")
        {
            OptionSaveProfile(arg_path.generic_u8string());

            options->action_performed = true;
            arg_index++;
        }

        /*-------------------------------------------------*\
        | -lx / --list-extended (no arguments)              |
        \*-------------------------------------------------*/
        else if(option == "--list-extended" || option == "-lx")
        {
            OptionListDevicesExtended(rgb_controllers);
            exit(0);
        }

        /*-------------------------------------------------*\
        | --direction                                       |
        \*-------------------------------------------------*/
        else if(option == "--direction")
        {
            if(!OptionDirection(&current_devices, argument, options))
            {
                return RET_FLAG_PRINT_HELP;
            }

            arg_index++;
        }

        /*-------------------------------------------------*\
        | --led                                             |
        \*-------------------------------------------------*/
        else if(option == "--led")
        {
            if(!OptionLed(&current_devices, argument))
            {
                return RET_FLAG_PRINT_HELP;
            }

            arg_index++;
        }

        /*-------------------------------------------------*\
        | --save-to-device (no arguments)                   |
        \*-------------------------------------------------*/
        else if(option == "--save-to-device")
        {
            OptionSaveToDevice(&current_devices, options);
        }

        /*-------------------------------------------------*\
        | --rename-device                                   |
        \*-------------------------------------------------*/
        else if(option == "--rename-device")
        {
            if(arg_index + 1 >= preserve_argc || !OptionRenameDevice(&current_devices, argument, rgb_controllers))
            {
                return RET_FLAG_PRINT_HELP;
            }

            arg_index++;
        }

        /*-------------------------------------------------*\
        | --rename-zone                                     |
        \*-------------------------------------------------*/
        else if(option == "--rename-zone")
        {
            if(arg_index + 1 >= preserve_argc || !OptionRenameZone(&current_devices, argument, rgb_controllers))
            {
                return RET_FLAG_PRINT_HELP;
            }

            arg_index++;
        }

        /*-------------------------------------------------*\
        | --segments                                        |
        \*-------------------------------------------------*/
        else if(option == "--segments")
        {
            if(arg_index + 1 >= preserve_argc || !OptionSegments(&current_devices, argument, rgb_controllers))
            {
                return RET_FLAG_PRINT_HELP;
            }

            arg_index++;
        }

        /*-------------------------------------------------*\
        | --rescan (no arguments)                           |
        \*-------------------------------------------------*/
        else if(option == "--rescan")
        {
            OptionRescan();
            options->action_performed = true;
        }

        /*-------------------------------------------------*\
        | --delete-profile                                  |
        \*-------------------------------------------------*/
        else if(option == "--delete-profile")
        {
            if(!OptionDeleteProfile(arg_path.generic_u8string()))
            {
                return RET_FLAG_PRINT_HELP;
            }

            options->action_performed = true;
            arg_index++;
        }

        /*-------------------------------------------------*\
        | --rename-profile (two arguments)                  |
        \*-------------------------------------------------*/
        else if(option == "--rename-profile")
        {
            std::string new_name = (arg_index + 2 < preserve_argc) ? preserve_argv[arg_index + 2] : "";

            if(!OptionRenameProfile(arg_path.generic_u8string(), new_name))
            {
                return RET_FLAG_PRINT_HELP;
            }

            options->action_performed = true;
            arg_index += 2;
        }

        /*-------------------------------------------------*\
        | --list-auto-profiles (no arguments)               |
        \*-------------------------------------------------*/
        else if(option == "--list-auto-profiles")
        {
            OptionListAutoProfiles();
            exit(0);
        }

        /*-------------------------------------------------*\
        | --auto-profile (two arguments)                    |
        \*-------------------------------------------------*/
        else if(option == "--auto-profile")
        {
            std::string profile_name = (arg_index + 2 < preserve_argc) ? preserve_argv[arg_index + 2] : "";

            if(!OptionAutoProfile(argument, profile_name))
            {
                return RET_FLAG_PRINT_HELP;
            }

            options->action_performed = true;
            arg_index += 2;
        }

        /*-------------------------------------------------*\
        | --list-detectors (optional filter)                |
        \*-------------------------------------------------*/
        else if(option == "--list-detectors")
        {
            std::string filter = "";

            if(arg_index + 1 < preserve_argc && argument.rfind("-", 0) != 0)
            {
                filter = argument;
            }

            OptionListDetectors(filter);
            exit(0);
        }

        /*-------------------------------------------------*\
        | --enable-detector / --disable-detector            |
        \*-------------------------------------------------*/
        else if(option == "--enable-detector" || option == "--disable-detector")
        {
            if(argument.empty() || !OptionSetDetector(argument, option == "--enable-detector"))
            {
                return RET_FLAG_PRINT_HELP;
            }

            options->action_performed = true;
            arg_index++;
        }

        /*-------------------------------------------------*\
        | --list-settings (no arguments)                    |
        \*-------------------------------------------------*/
        else if(option == "--list-settings")
        {
            OptionListSettings();
            exit(0);
        }

        /*-------------------------------------------------*\
        | --get-setting                                     |
        \*-------------------------------------------------*/
        else if(option == "--get-setting")
        {
            if(argument.empty() || !OptionGetSetting(argument))
            {
                return RET_FLAG_PRINT_HELP;
            }

            exit(0);
        }

        /*-------------------------------------------------*\
        | --set-setting (two arguments)                     |
        \*-------------------------------------------------*/
        else if(option == "--set-setting")
        {
            if(arg_index + 2 >= preserve_argc || !OptionSetSetting(argument, preserve_argv[arg_index + 2]))
            {
                return RET_FLAG_PRINT_HELP;
            }

            options->action_performed = true;
            arg_index += 2;
        }

        /*-------------------------------------------------*\
        | Invalid option                                    |
        \*-------------------------------------------------*/
        else
        {
            if((option == "--localconfig")
             ||(option == "--nodetect")
             ||(option == "--noautoconnect")
             ||(option == "--server")
             ||(option == "--gui")
             ||(option == "--i2c-tools" || option == "--yolo")
             ||(option == "--startminimized")
             ||(option == "--print-source")
             ||(option == "--verbose" || option == "-v")
             ||(option == "--very-verbose" || option == "-vv")
             ||(option == "--help" || option == "-h")
             ||(option == "--version" || option == "-V")
             ||(option == "--autostart-check")
             ||(option == "--autostart-disable"))
            {
                /*-----------------------------------------*\
                | Do nothing, these are pre-detection       |
                | arguments and this parser should ignore   |
                | them                                      |
                \*-----------------------------------------*/
            }
            else if((option == "--server-port")
                  ||(option == "--server-host")
                  ||(option == "--loglevel")
                  ||(option == "--config")
                  ||(option == "--client")
#ifdef __linux__
                  ||(option == "--generate-udev-rules")
#endif
                  ||(option == "--autostart-enable"))
            {
                /*-----------------------------------------*\
                | Increment index for pre-detection         |
                | arguments with parameter                  |
                \*-----------------------------------------*/
                arg_index++;
            }
            else
            {
                /*-----------------------------------------*\
                | If the argument is not a pre-detection    |
                | argument, throw an error and print help   |
                \*-----------------------------------------*/
                std::cout << "Error: Invalid option: " + option << std::endl;
                return RET_FLAG_PRINT_HELP;
            }
        }

        arg_index++;
    }

    /*-----------------------------------------------------*\
    | If a device was specified, check to verify that a     |
    | corresponding option was also specified               |
    \*-----------------------------------------------------*/
    while(!current_devices.empty())
    {
        options->devices.push_back(current_devices.back());
        current_devices.pop_back();
    }

    if(options->hasDevice)
    {
        for(std::size_t option_idx = 0; option_idx < options->devices.size(); option_idx++)
        {
            if(!options->devices[option_idx].hasOption && !options->devices[option_idx].hasConfig)
            {
                std::cout << "Error: Device " + std::to_string(option_idx) + " specified, but neither mode nor color given" << std::endl;
                return RET_FLAG_PRINT_HELP;
            }
        }
        return 0;
    }
    else
    {
        return ret_flags;
    }
}

void ApplyOptions(DeviceOptions& options, std::vector<RGBController *>& rgb_controllers)
{
    RGBController* device = rgb_controllers[options.device];

    /*-----------------------------------------------------*\
    | Clear the active profile when applying device changes |
    \*-----------------------------------------------------*/
    ResourceManager::get()->GetProfileManager()->ClearActiveProfile();

    /*-----------------------------------------------------*\
    | Set mode first, in case it's 'direct' (which affects  |
    | SetColor below)                                       |
    \*-----------------------------------------------------*/
    unsigned int mode = ParseMode(options, rgb_controllers);

    /*-----------------------------------------------------*\
    | If the user has specified random colours and the      |
    | device supports that colour mode then swich to it     |
    | before evaluating if a colour needs to be set         |
    \*-----------------------------------------------------*/
    if(options.random_colors && (device->GetModeFlags(mode) & MODE_FLAG_HAS_RANDOM_COLOR))
    {
        device->SetModeColorMode(mode, MODE_COLORS_RANDOM);
    }

    /*-----------------------------------------------------*\
    | If the user has specified random colours and the      |
    | device supports that colour mode then swich to it     |
    | before evaluating if a colour needs to be set         |
    \*-----------------------------------------------------*/
    if((device->GetModeFlags(mode) & MODE_FLAG_HAS_BRIGHTNESS))
    {
        unsigned int new_brightness     = device->GetModeBrightnessMax(mode) - device->GetModeBrightnessMin(mode);
        new_brightness                 *= options.brightness;
        new_brightness                 /= 100;

        device->SetModeBrightness(mode, device->GetModeBrightnessMin(mode) + new_brightness);
    }

    if((device->GetModeFlags(mode) & MODE_FLAG_HAS_SPEED))
    {
        unsigned int new_speed          = device->GetModeSpeedMax(mode) - device->GetModeSpeedMin(mode);
        new_speed                      *= options.speed;
        new_speed                      /= 100;

        device->SetModeSpeed(mode, device->GetModeSpeedMin(mode) + new_speed);
    }

    /*-----------------------------------------------------*\
    | Set direction if requested and supported              |
    \*-----------------------------------------------------*/
    if(options.direction >= 0)
    {
        if(DirectionSupported(device->GetModeFlags(mode), options.direction))
        {
            device->SetModeDirection(mode, (unsigned int)options.direction);
        }
        else
        {
            std::cout << "Warning: Mode " << device->GetModeName(mode) << " of " << device->GetDisplayName()
                      << " does not support direction " << DirectionToString(options.direction) << std::endl;
        }
    }

    /*-----------------------------------------------------*\
    | Determine which color mode this mode uses and update  |
    | colors accordingly                                    |
    \*-----------------------------------------------------*/
    switch(device->GetModeColorMode(mode))
    {
        case MODE_COLORS_NONE:
            break;

        case MODE_COLORS_RANDOM:
            break;

        case MODE_COLORS_PER_LED:
            if(options.colors.size() != 0 && options.leds.size() != 0)
            {
                /*-----------------------------------------*\
                | Only the LEDs selected with --led change; |
                | numbers are relative to --zone if given   |
                \*-----------------------------------------*/
                unsigned int start_from = 0;
                unsigned int led_count  = device->GetLEDCount();

                if(options.zone >= 0)
                {
                    start_from          = device->GetZoneStartIndex(options.zone);
                    led_count           = device->GetLEDsInZone(options.zone);
                }

                for(std::size_t sel_idx = 0; sel_idx < options.leds.size(); sel_idx++)
                {
                    if(options.leds[sel_idx] >= led_count)
                    {
                        std::cout << "Warning: LED " << options.leds[sel_idx] << " is out of range (0-" << (led_count == 0 ? 0 : led_count - 1) << "), skipped" << std::endl;
                        continue;
                    }

                    std::size_t color_idx = std::min(sel_idx, options.colors.size() - 1);

                    device->SetColor(start_from + options.leds[sel_idx], ToRGBColor(std::get<0>(options.colors[color_idx]),
                                                                                    std::get<1>(options.colors[color_idx]),
                                                                                    std::get<2>(options.colors[color_idx])));
                }
            }
            else if(options.colors.size() != 0)
            {
                unsigned int start_from;
                unsigned int led_count;

                if(options.zone < 0)
                {
                    start_from          = 0;
                    led_count           = device->GetLEDCount();
                }
                else
                {
                    start_from          = device->GetZoneStartIndex(options.zone);
                    led_count           = device->GetLEDsInZone(options.zone);
                }

                for(unsigned int led_idx = 0; led_idx < led_count; led_idx++)
                {
                    unsigned int color_idx = led_idx;

                    if(color_idx >= options.colors.size())
                    {
                        color_idx       = (unsigned int)options.colors.size() - 1;
                    }

                    device->SetColor((unsigned int)(start_from + led_idx), ToRGBColor(std::get<0>(options.colors[color_idx]),
                                                                                      std::get<1>(options.colors[color_idx]),
                                                                                      std::get<2>(options.colors[color_idx])));
                }
            }
            break;

        case MODE_COLORS_MODE_SPECIFIC:
            if(options.colors.size() >= device->GetModeColorsMin(mode) && options.colors.size() <= device->GetModeColorsMax(mode))
            {
                device->SetModeColorsCount(mode, (unsigned int)options.colors.size());

                for(std::size_t color_idx = 0; color_idx < options.colors.size(); color_idx++)
                {
                    device->SetModeColor(mode, (unsigned int)color_idx, ToRGBColor(std::get<0>(options.colors[color_idx]), std::get<1>(options.colors[color_idx]), std::get<2>(options.colors[color_idx])));
                }
            }
            else
            {
                std::cout << "Wrong number of colors specified for mode " + device->GetModeName(mode) << std::endl;
                std::cout << "Please provide between " + std::to_string(device->GetModeColorsMin(mode)) + " and " + std::to_string(device->GetModeColorsMax(mode)) + " colors" << std::endl;
                exit(0);
            }
            break;
    }

    /*-----------------------------------------------------*\
    | Set device mode                                       |
    \*-----------------------------------------------------*/
    device->SetActiveMode(mode);

    /*-----------------------------------------------------*\
    | Set device per-LED colors if necessary                |
    \*-----------------------------------------------------*/
    if(device->GetModeColorMode(mode) == MODE_COLORS_PER_LED)
    {
        device->DeviceUpdateLEDs();
    }
    else if(options.leds.size() != 0)
    {
        std::cout << "Warning: --led needs a per-LED mode (such as Direct); mode " << device->GetModeName(mode) << " of " << device->GetDisplayName() << " is not" << std::endl;
    }

    /*-----------------------------------------------------*\
    | Save the mode to the device's memory if requested     |
    \*-----------------------------------------------------*/
    if(options.save_to_device)
    {
        if(device->GetModeFlags(mode) & MODE_FLAG_MANUAL_SAVE)
        {
            device->SaveMode();
            std::cout << "Mode " << device->GetModeName(mode) << " saved to " << device->GetDisplayName() << std::endl;
        }
        else if(device->GetModeFlags(mode) & MODE_FLAG_AUTOMATIC_SAVE)
        {
            std::cout << device->GetDisplayName() << " saves mode " << device->GetModeName(mode) << " automatically" << std::endl;
        }
        else
        {
            std::cout << "Warning: Mode " << device->GetModeName(mode) << " of " << device->GetDisplayName() << " cannot be saved to the device" << std::endl;
        }
    }
}


unsigned int cli_pre_detection(int argc, char* argv[])
{
    /*-----------------------------------------------------*\
    | Process only the arguments that should be performed   |
    | prior to detecting devices and/or starting clients    |
    \*-----------------------------------------------------*/
    int             arg_index       = 1;
    unsigned int    cfg_args        = 0;
    unsigned int    ret_flags       = 0;
    bool            server_start    = false;
    bool            print_help      = false;

    preserve_argc                   = argc;
    preserve_argv                   = argv;

#ifdef _WIN32
    int             fake_argc;
    wchar_t**       argvw           = CommandLineToArgvW(GetCommandLineW(), &fake_argc);
#endif

    while(arg_index < argc)
    {
        std::string option          = argv[arg_index];
        std::string argument        = "";

        LOG_DEBUG("[CLI] Parsing CLI option: %s", option.c_str());

        /*-------------------------------------------------*\
        | Handle options that take an argument              |
        \*-------------------------------------------------*/
        if(arg_index + 1 < argc)
        {
            argument                = argv[arg_index + 1];
        }

        /*-------------------------------------------------*\
        | --localconfig                                     |
        \*-------------------------------------------------*/
        if(option == "--localconfig")
        {
            ResourceManager::get()->SetConfigurationDirectory("./");
            cfg_args++;
        }

        /*-------------------------------------------------*\
        | --config                                          |
        \*-------------------------------------------------*/
        else if(option == "--config")
        {
            cfg_args               += 2;
            arg_index++;
#ifdef _WIN32
            filesystem::path config_path(argvw[arg_index]);
#else
            filesystem::path config_path(argument);
#endif

            if(filesystem::is_directory(config_path))
            {
                ResourceManager::get()->SetConfigurationDirectory(config_path);
                LOG_INFO("[CLI] Setting config directory to %s",argument.c_str()); // TODO: Use config_path in logs somehow
            }
            else
            {
                LOG_ERROR("[CLI] '%s' is not a valid directory",argument.c_str()); // TODO: Use config_path in logs somehow
                print_help = true;
                break;
            }
        }

        /*-------------------------------------------------*\
        | --nodetect                                        |
        \*-------------------------------------------------*/
        else if(option == "--nodetect")
        {
            ret_flags              |= RET_FLAG_NO_DETECT;
            cfg_args++;
        }

        /*-------------------------------------------------*\
        | --noautoconnect                                   |
        \*-------------------------------------------------*/
        else if(option == "--noautoconnect")
        {
            ret_flags              |= RET_FLAG_NO_AUTO_CONNECT;
            cfg_args++;
        }

        /*-------------------------------------------------*\
        | --client                                          |
        \*-------------------------------------------------*/
        else if(option == "--client")
        {
            NetworkClient* client   = new NetworkClient();

            std::size_t     pos     = argument.find(":");
            std::string     ip      = argument.substr(0, pos);
            unsigned short  port_val;

            if(pos == argument.npos)
            {
                port_val            = OPENRGB_SDK_PORT;
            }
            else
            {
                std::string port    = argument.substr(argument.find(":") + 1);
                port_val            = std::stoi(port);
            }

            std::string titleString = "OpenRGB ";
            titleString.append(VERSION_STRING);

            client->SetIP(ip.c_str());
            client->SetName(titleString.c_str());
            client->SetPort(port_val);

            client->StartClient();

            for(int timeout = 0; timeout < 100; timeout++)
            {
                if(client->GetConnected())
                {
                    break;
                }
                std::this_thread::sleep_for(10ms);
            }

            ResourceManager::get()->RegisterNetworkClient(client);

            cfg_args++;
            arg_index++;
        }

        /*-------------------------------------------------*\
        | --server (no arguments)                           |
        \*-------------------------------------------------*/
        else if(option == "--server")
        {
            server_start            = true;
        }

        /*-------------------------------------------------*\
        | --server-port                                     |
        \*-------------------------------------------------*/
        else if(option == "--server-port")
        {
            if(argument != "")
            {
                try
                {
                    int port        = std::stoi(argument);

                    if(port >= 1024 && port <= 65535)
                    {
                        ResourceManager::get()->SetDefaultServerPort(port);
                        server_start = true;
                    }
                    else
                    {
                        std::cout << "Error: Port out of range: " << port << " (1024-65535)" << std::endl;
                        print_help  = true;
                        break;
                    }
                }
                catch(std::invalid_argument& /*e*/)
                {
                    std::cout << "Error: Invalid data in --server-port argument (expected a number in range 1024-65535)" << std::endl;
                    print_help      = true;
                    break;
                }
            }
            else
            {
                std::cout << "Error: Missing argument for --server-port" << std::endl;
                print_help          = true;
                break;
            }

            cfg_args++;
            arg_index++;
        }

        /*-------------------------------------------------*\
        | --server-host                                     |
        \*-------------------------------------------------*/
        else if(option == "--server-host")
        {
            if(argument != "")
            {
                ResourceManager::get()->SetDefaultServerHost(argument);
                server_start        = true;
            }
            else
            {
                std::cout << "Error: Missing argument for --server-host" << std::endl;
                print_help          = true;
                break;
            }

            cfg_args++;
            arg_index++;
        }

        /*-------------------------------------------------*\
        | --loglevel                                        |
        \*-------------------------------------------------*/
        else if(option == "--loglevel")
        {
            if(argument != "")
            {
                try
                {
                    int level       = std::stoi(argument);

                    if(level >= 0 && level <= LL_TRACE)
                    {
                        LogManager::get()->SetLogLevel(level);
                    }
                    else
                    {
                        LOG_ERROR("[CLI] LogLevel out of range: %d (0-6)", level);
                        print_help  = true;
                        break;
                    }
                }
                catch(std::invalid_argument& /*e*/)
                {
                    if(!strcasecmp(argument.c_str(), "fatal"))
                    {
                        LogManager::get()->SetLogLevel(LL_FATAL);
                    }
                    else if(!strcasecmp(argument.c_str(), "error"))
                    {
                        LogManager::get()->SetLogLevel(LL_ERROR);
                    }
                    else if(!strcasecmp(argument.c_str(), "warning"))
                    {
                        LogManager::get()->SetLogLevel(LL_WARNING);
                    }
                    else if(!strcasecmp(argument.c_str(), "info"))
                    {
                        LogManager::get()->SetLogLevel(LL_INFO);
                    }
                    else if(!strcasecmp(argument.c_str(), "verbose"))
                    {
                        LogManager::get()->SetLogLevel(LL_VERBOSE);
                    }
                    else if(!strcasecmp(argument.c_str(), "debug"))
                    {
                        LogManager::get()->SetLogLevel(LL_DEBUG);
                    }
                    else if(!strcasecmp(argument.c_str(), "trace"))
                    {
                        LogManager::get()->SetLogLevel(LL_TRACE);
                    }
                    else
                    {
                        LOG_ERROR("[CLI] Invalid loglevel");
                        print_help  = true;
                        break;
                    }
                }
            }
            else
            {
                LOG_ERROR("[CLI] Missing argument for --loglevel");
                print_help          = true;
                break;
            }
            cfg_args+= 2;
            arg_index++;
        }

        /*-------------------------------------------------*\
        | --autostart-check (no arguments)                  |
        \*-------------------------------------------------*/
        else if(option == "--autostart-check")
        {
            AutoStart auto_start("OpenRGB");

            if(auto_start.IsAutoStartEnabled())
            {
                std::cout << "Autostart is enabled." << std::endl;
            }
            else
            {
                std::cout << "Autostart is disabled." << std::endl;
            }
        }

        /*-------------------------------------------------*\
        | --autostart-disable (no arguments)                |
        \*-------------------------------------------------*/
        else if(option == "--autostart-disable")
        {
            AutoStart auto_start("OpenRGB");

            if(auto_start.DisableAutoStart())
            {
                std::cout << "Autostart disabled." << std::endl;
            }
            else
            {
                std::cout << "Autostart failed to disable." << std::endl;
            }
        }

        /*-------------------------------------------------*\
        | --autostart-enable                                |
        \*-------------------------------------------------*/
        else if(option == "--autostart-enable")
        {
            if(argument != "")
            {
                std::string desc                = "OpenRGB ";
                desc                           += VERSION_STRING;
                desc                           += ", for controlling RGB lighting.";

                AutoStart       auto_start("OpenRGB");
                AutoStartInfo   auto_start_interface;

                auto_start_interface.args       = argument;
                auto_start_interface.category   = "Utility;";
                auto_start_interface.desc       = desc;
                auto_start_interface.icon       = "OpenRGB";
                auto_start_interface.path       = auto_start.GetExePath();

                if(auto_start.EnableAutoStart(auto_start_interface))
                {
                    std::cout << "Autostart enabled." << std::endl;
                }
                else
                {
                    std::cout << "Autostart failed to enable." << std::endl;
                }
            }
            else
            {
                std::cout << "Error: Missing argument for --autostart-enable" << std::endl;
                print_help = true;
                break;
            }

            cfg_args++;
            arg_index++;
        }

#ifdef __linux__
        /*-------------------------------------------------*\
        | --generate-udev-rules                             |
        \*-------------------------------------------------*/
        else if(option == "--generate-udev-rules")
        {
            if(argument == "")
            {
                std::cout << "Error: Missing argument for --generate-udev-rules" << std::endl;
                print_help          = true;
                break;
            }

            OptionGenerateUdevRules(argument);

            /*---------------------------------------------*\
            | No device detection is needed, exit           |
            | immediately after writing the udev rules      |
            \*---------------------------------------------*/
            exit(0);
        }

        /*-------------------------------------------------*\
        | --print-udev-rules (no arguments)                 |
        \*-------------------------------------------------*/
        else if(option == "--print-udev-rules")
        {
            OptionPrintUdevRules();

            /*---------------------------------------------*\
            | No device detection is needed, exit           |
            | immediately after printing the udev rules     |
            \*---------------------------------------------*/
            exit(0);
        }
#endif

        /*-------------------------------------------------*\
        | --gui (no arguments)                              |
        \*-------------------------------------------------*/
        else if(option == "--gui")
        {
            ret_flags              |= RET_FLAG_START_GUI;
        }

        /*-------------------------------------------------*\
        | --i2c-tools / --yolo (no arguments)               |
        \*-------------------------------------------------*/
        else if(option == "--i2c-tools" || option == "--yolo")
        {
            ret_flags              |= RET_FLAG_START_GUI | RET_FLAG_I2C_TOOLS;
        }

        /*-------------------------------------------------*\
        | --startminimized (no arguments)                   |
        \*-------------------------------------------------*/
        else if(option == "--startminimized")
        {
            ret_flags |= RET_FLAG_START_GUI | RET_FLAG_START_MINIMIZED;
        }

        /*-------------------------------------------------*\
        | -h / --help (no arguments)                        |
        \*-------------------------------------------------*/
        else if(option == "--help" || option == "-h")
        {
            print_help = true;
            break;
        }

        /*-------------------------------------------------*\
        | -V / --version (no arguments)                     |
        \*-------------------------------------------------*/
        else if(option == "--version" || option == "-V")
        {
            OptionVersion();
            exit(0);
        }

        /*-------------------------------------------------*\
        | -v / --verbose (no arguments)                     |
        \*-------------------------------------------------*/
        else if(option == "--verbose" || option == "-v")
        {
            LogManager::get()->SetVerbosity(LL_VERBOSE);
            cfg_args++;
        }

        /*-------------------------------------------------*\
        | -vv / --very-verbose (no arguments)               |
        \*-------------------------------------------------*/
        else if(option == "--very-verbose" || option == "-vv")
        {
            LogManager::get()->SetVerbosity(LL_TRACE);
            cfg_args++;
        }

        /*-------------------------------------------------*\
        | --print-source (no arguments)                     |
        \*-------------------------------------------------*/
        else if(option == "--print-source")
        {
            LogManager::get()->SetPrintSource(true);
            cfg_args++;
        }

        /*-------------------------------------------------*\
        | Any unrecognized arguments trigger the            |
        | post-detection CLI                                |
        \*-------------------------------------------------*/
        else
        {
            ret_flags              |= RET_FLAG_CLI_POST_DETECTION;
        }

        arg_index++;
    }

    if(print_help)
    {
        OptionHelp();
        exit(0);
    }

    if(server_start)
    {
        ret_flags |= RET_FLAG_START_SERVER;
    }

    if((argc - cfg_args) <= 1)
    {
        ret_flags |= RET_FLAG_START_GUI;
    }

    return(ret_flags);
}

unsigned int cli_post_detection()
{
    /*-----------------------------------------------------*\
    | Wait for device detection                             |
    \*-----------------------------------------------------*/
    ResourceManager::get()->WaitForDetection();

    /*-----------------------------------------------------*\
    | Get controller list from resource manager             |
    \*-----------------------------------------------------*/
    std::vector<RGBController *> rgb_controllers = ResourceManager::get()->GetRGBControllers();

    /*-----------------------------------------------------*\
    | Process the argument options                          |
    \*-----------------------------------------------------*/
    Options options;
    unsigned int ret_flags = ProcessOptions(&options, rgb_controllers);

    /*-----------------------------------------------------*\
    | If the return flags are set, exit CLI mode without    |
    | processing device updates from CLI input.             |
    \*-----------------------------------------------------*/
    switch(ret_flags)
    {
        case 0:
            break;

        case RET_FLAG_PRINT_HELP:
            std::cout << "Run `OpenRGB --help` for syntax" << std::endl;
            exit(-1);
            break;

        default:
            return ret_flags;
            break;
    }

    /*-----------------------------------------------------*\
    | If the options has one or more specific devices, loop |
    | through all of the specific devices and apply         |
    | settings.  Otherwise, apply settings to all devices.  |
    \*-----------------------------------------------------*/
    if(options.hasDevice)
    {
        for(unsigned int device_idx = 0; device_idx < options.devices.size(); device_idx++)
        {
            if(options.devices[device_idx].hasOption)
            {
                ApplyOptions(options.devices[device_idx], rgb_controllers);
            }
        }
    }
    else if(!options.profile_loaded && (!options.action_performed || options.allDeviceOptions.hasOption))
    {
        for(unsigned int device_idx = 0; device_idx < rgb_controllers.size(); device_idx++)
        {
            options.allDeviceOptions.device = device_idx;
            ApplyOptions(options.allDeviceOptions, rgb_controllers);
        }
    }

    /*-----------------------------------------------------*\
    | If there is a save name set, save the profile         |
    \*-----------------------------------------------------*/
    if(profile_save_name != "")
    {
        if(ResourceManager::get()->GetProfileManager()->SaveProfile(profile_save_name))
        {
            LOG_INFO("[CLI] Profile saved successfully");
        }
        else
        {
            LOG_ERROR("[CLI] Profile saving failed");
        }
    }

    std::this_thread::sleep_for(1s);

    return 0;
}
