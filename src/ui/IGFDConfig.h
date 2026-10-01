// IGFDConfig.h — ImGuiFileDialog configuration (CUSTOM_IMGUIFILEDIALOG_CONFIG).
// Phosphor glyphs make the dialog match the application theme.
#pragma once

#include "IconsPhosphor.h"

#define USE_STD_FILESYSTEM

#define USE_PLACES_FEATURE
#define USE_PLACES_BOOKMARKS
#define USE_PLACES_DEVICES
#define PLACES_PANE_DEFAULT_SHOWN false
#define placesButtonString ICON_PH_BOOKMARK_SIMPLE " Places"
#define placesButtonHelpString "Places (bookmarks and devices)"
#define addPlaceButtonString ICON_PH_PLUS
#define removePlaceButtonString ICON_PH_MINUS
#define validatePlaceButtonString ICON_PH_CHECK
#define editPlaceButtonString ICON_PH_PENCIL_SIMPLE

#define USE_EXPLORATION_BY_KEYS
#define IGFD_KEY_UP ImGuiKey_UpArrow
#define IGFD_KEY_DOWN ImGuiKey_DownArrow
#define IGFD_KEY_ENTER ImGuiKey_Enter
#define IGFD_KEY_BACKSPACE ImGuiKey_Backspace

#define USE_DIALOG_EXIT_WITH_KEY
#define IGFD_EXIT_KEY ImGuiKey_Escape

#define createDirButtonString ICON_PH_FOLDER_PLUS
#define resetButtonString ICON_PH_ARROW_COUNTER_CLOCKWISE
#define devicesButtonString ICON_PH_HARD_DRIVES
#define editPathButtonString ICON_PH_PENCIL_SIMPLE
#define searchString ICON_PH_MAGNIFYING_GLASS
#define dirEntryString ICON_PH_FOLDER " "
#define linkEntryString ICON_PH_LINK " "
#define fileEntryString ICON_PH_FILE " "
#define fileNameString "File name"
#define dirNameString "Folder"
#define buttonResetSearchString "Reset search"
#define buttonDriveString "Devices"
#define buttonEditPathString "Edit path\nYou can also right click on path buttons"
#define buttonResetPathString "Reset to current directory"
#define buttonCreateDirString "Create folder"

#define okButtonString ICON_PH_CHECK " OK"
#define okButtonWidth 0.0f
#define cancelButtonString ICON_PH_X " Cancel"
#define cancelButtonWidth 0.0f
#define okCancelButtonAlignement 1.0f

#define USE_CUSTOM_SORTING_ICON
#define tableHeaderAscendingIcon ICON_PH_CARET_UP " "
#define tableHeaderDescendingIcon ICON_PH_CARET_DOWN " "
#define tableHeaderFileNameString " Name"
#define tableHeaderFileTypeString " Type"
#define tableHeaderFileSizeString " Size"
#define tableHeaderFileDateTimeString " Date"
#define fileSizeBytes "B"
#define fileSizeKiloBytes "kB"
#define fileSizeMegaBytes "MB"
#define fileSizeGigaBytes "GB"
