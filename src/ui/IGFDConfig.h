// IGFDConfig.h — ImGuiFileDialog configuration (CUSTOM_IMGUIFILEDIALOG_CONFIG).
// Phosphor glyphs make the dialog match the application theme. The visible texts follow the
// Language menu through ui/IGFDGlue.h: plain strings expand to igfd::text("English") and every
// button goes through igfd::button(), which translates the part before "##". Strings that the
// library concatenates with other literals (OK / Cancel + "##validationdialog", the overwrite
// title) stay literals here and are translated inside igfd::button(); the overwrite title is a
// language-neutral glyph.
#pragma once

#include "IconsPhosphor.h"
#include "ui/IGFDGlue.h"

#define USE_STD_FILESYSTEM

#define IMGUI_BUTTON evobox::igfd::button
#define IMGUI_TOGGLE_BUTTON evobox::igfd::toggleButton

#define USE_PLACES_FEATURE
#define USE_PLACES_BOOKMARKS
#define USE_PLACES_DEVICES
#define PLACES_PANE_DEFAULT_SHOWN false
#define placesButtonString ICON_PH_BOOKMARK_SIMPLE " Places"
#define placesButtonHelpString evobox::igfd::text("Places (bookmarks and devices)")
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
#define fileNameString evobox::igfd::text("File name")
#define dirNameString evobox::igfd::text("Folder")
#define buttonResetSearchString evobox::igfd::text("Reset search")
#define buttonDriveString evobox::igfd::text("Devices")
#define buttonEditPathString evobox::igfd::text("Edit path\nYou can also right click on path buttons")
#define buttonResetPathString evobox::igfd::text("Reset to current directory")
#define buttonCreateDirString evobox::igfd::text("Create folder")

#define okButtonString ICON_PH_CHECK " OK"
#define okButtonWidth 0.0f
#define cancelButtonString ICON_PH_X " Cancel"
#define cancelButtonWidth 0.0f
#define okCancelButtonAlignement 1.0f

#define OverWriteDialogTitleString ICON_PH_WARNING
#define OverWriteDialogMessageString evobox::igfd::text("The selected file already exists. Overwrite it?")
#define OverWriteDialogConfirmButtonString ICON_PH_CHECK " Confirm"
#define OverWriteDialogCancelButtonString ICON_PH_X " Cancel"

#define USE_CUSTOM_SORTING_ICON
#define tableHeaderAscendingIcon ICON_PH_CARET_UP " "
#define tableHeaderDescendingIcon ICON_PH_CARET_DOWN " "
#define tableHeaderFileNameString evobox::igfd::text(" Name")
#define tableHeaderFileTypeString evobox::igfd::text(" Type")
#define tableHeaderFileSizeString evobox::igfd::text(" Size")
#define tableHeaderFileDateString evobox::igfd::text(" Date")
#define fileSizeBytes "B"
#define fileSizeKiloBytes "kB"
#define fileSizeMegaBytes "MB"
#define fileSizeGigaBytes "GB"
