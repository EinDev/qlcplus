add_executable(audio_test WIN32
    ../mastertimer/mastertimer_stub.cpp ../mastertimer/mastertimer_stub.h
    audio_test.cpp audio_test.h
)
target_include_directories(audio_test PRIVATE
    ../../../plugins/interfaces
    ../../audio/src
    ../../src
    ../mastertimer
)

target_link_libraries(audio_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Gui
    Qt${QT_MAJOR_VERSION}::Test
    qlcplusaudio
    qlcplusengine
)
