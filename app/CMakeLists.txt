find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()  # turns on AUTOMOC (needed for Q_OBJECT classes)

# All windows and dialogs live in a library so that future GUI tests can
# link against them without the exe's main().
qt_add_library(finrenamer_ui STATIC
    QtHelpers.h
    MainWindow.h MainWindow.cpp
    CaseDialog.h CaseDialog.cpp
    PersonDialog.h PersonDialog.cpp
    AccountDialog.h AccountDialog.cpp
)
target_include_directories(finrenamer_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(finrenamer_ui PUBLIC finrenamer_core Qt6::Widgets)

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
