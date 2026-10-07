find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets OPTIONAL_COMPONENTS Pdf PdfWidgets)
qt_standard_project_setup()  # turns on AUTOMOC (needed for Q_OBJECT classes)

# All windows and dialogs live in a library so that future GUI tests can
# link against them without the exe's main().
qt_add_library(finrenamer_ui STATIC
    QtHelpers.h
    HomeWindow.h HomeWindow.cpp
    CasePickerDialog.h CasePickerDialog.cpp
    MainWindow.h MainWindow.cpp
    CaseDialog.h CaseDialog.cpp
    PersonDialog.h PersonDialog.cpp
    AccountDialog.h AccountDialog.cpp
    CaseFolderList.h CaseFolderList.cpp
    DeficiencyDialog.h DeficiencyDialog.cpp
    DateWidgets.h DateWidgets.cpp
    HistoryDialog.h HistoryDialog.cpp
    RenameWindow.h RenameWindow.cpp
    PdfPreview.h PdfPreview.cpp
    UpdateNamesDialog.h UpdateNamesDialog.cpp
)
target_include_directories(finrenamer_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(finrenamer_ui PUBLIC finrenamer_core Qt6::Widgets)

# The in-app PDF preview needs the Qt PDF add-on (Qt installer: Additional
# Libraries > Qt PDF). Without it the app still builds; the pane says so.
if(TARGET Qt6::PdfWidgets)
    target_link_libraries(finrenamer_ui PUBLIC Qt6::Pdf Qt6::PdfWidgets)
    target_compile_definitions(finrenamer_ui PUBLIC FINRENAMER_HAVE_QTPDF)
    message(STATUS "FinRenamer: Qt PDF found, PDF preview enabled")
else()
    message(WARNING "FinRenamer: Qt PDF not found, so the PDF preview pane is disabled. "
                    "Add it with the Qt Maintenance Tool (Additional Libraries > Qt PDF).")
endif()

# WIN32 = a normal windowed app, no console window behind it.
qt_add_executable(FinRenamer WIN32 main.cpp)
target_link_libraries(FinRenamer PRIVATE finrenamer_ui)

# Copy the Qt DLLs and plugins next to FinRenamer.exe after every build, so it
# runs straight from Visual Studio (and the folder can be zipped for coworkers).
if(WIN32)
    if(TARGET Qt6::windeployqt)
        set(_windeployqt "$<TARGET_FILE:Qt6::windeployqt>")
    else()
        find_program(_windeployqt windeployqt HINTS "${QT6_INSTALL_PREFIX}/bin")
    endif()
    if(_windeployqt)
        add_custom_command(TARGET FinRenamer POST_BUILD
            COMMAND "${_windeployqt}" --no-translations "$<TARGET_FILE:FinRenamer>"
            COMMENT "Copying Qt DLLs next to FinRenamer.exe"
            VERBATIM)
    endif()
endif()

# ---------------------------------------------------------------------------
# Packaging: `cmake --install build --config Release --component FinRenamer
# --prefix dist` produces a
# clean folder with only what the program needs:
#   dist/bin/FinRenamer.exe, the Qt DLLs and plugins, and the Microsoft C++
#   runtime DLLs (copied next to the exe, so no admin rights or vc_redist are
#   needed on the computer it's installed on).
# installer/FinRenamer.iss (Inno Setup) turns that folder into Setup.exe.

# Everything the program needs is in the "FinRenamer" install component;
# the libraries CMake downloads (SQLiteCpp, Catch2) have their own install
# rules for developers, which this keeps out of the package.
install(TARGETS FinRenamer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT FinRenamer)

qt_generate_deploy_app_script(
    TARGET FinRenamer
    OUTPUT_SCRIPT finrenamer_deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
    NO_TRANSLATIONS
    NO_COMPILER_RUNTIME)  # the runtime comes from InstallRequiredSystemLibraries below
install(SCRIPT ${finrenamer_deploy_script} COMPONENT FinRenamer)

if(MSVC)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${CMAKE_INSTALL_BINDIR})
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT FinRenamer)
    set(CMAKE_INSTALL_UCRT_LIBRARIES FALSE)  # the Universal CRT is built into Windows 10/11
    include(InstallRequiredSystemLibraries)
endif()

# `installer` target: Release build -> dist folder -> Setup.exe, in one step.
# In Visual Studio: pick the Release configuration, then build the "installer"
# target (Solution Explorer's CMake Targets view, or `cmake --build --preset
# release --target installer`).
if(WIN32)
    find_program(FINRENAMER_ISCC ISCC
        HINTS "$ENV{ProgramFiles\(x86\)}/Inno Setup 6" "$ENV{ProgramFiles}/Inno Setup 6"
              "$ENV{LOCALAPPDATA}/Programs/Inno Setup 6")
    if(FINRENAMER_ISCC)
        add_custom_target(installer
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${PROJECT_SOURCE_DIR}/dist"
            COMMAND ${CMAKE_COMMAND} --install "${PROJECT_BINARY_DIR}" --config $<CONFIG>
                    --component FinRenamer --prefix "${PROJECT_SOURCE_DIR}/dist"
            COMMAND "${FINRENAMER_ISCC}" /Q
                    "/DMyAppVersion=${PROJECT_VERSION}"
                    "/DDistDir=${PROJECT_SOURCE_DIR}/dist"
                    "${PROJECT_SOURCE_DIR}/installer/FinRenamer.iss"
            DEPENDS FinRenamer
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/installer"
            COMMENT "Building the FinRenamer installer (installer/Output)"
            VERBATIM)
    else()
        message(STATUS "FinRenamer: Inno Setup not found; the 'installer' target is unavailable")
    endif()
endif()
