find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets OPTIONAL_COMPONENTS Pdf PdfWidgets)
qt_standard_project_setup()  # turns on AUTOMOC (needed for Q_OBJECT classes)

# All windows and dialogs live in a library so that future GUI tests can
# link against them without the exe's main().
qt_add_library(finrenamer_ui STATIC
    QtHelpers.h
    MainWindow.h MainWindow.cpp
    CaseDialog.h CaseDialog.cpp
    PersonDialog.h PersonDialog.cpp
    AccountDialog.h AccountDialog.cpp
    DateWidgets.h DateWidgets.cpp
    HistoryDialog.h HistoryDialog.cpp
    RenameWindow.h RenameWindow.cpp
    PdfPreview.h PdfPreview.cpp
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
