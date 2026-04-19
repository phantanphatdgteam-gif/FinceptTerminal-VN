# Fincept Terminal — test suite (opt-in)
#
# Enable by configuring with -DFINCEPT_BUILD_TESTS=ON. Disabled by default so
# normal dev / release builds remain unchanged. Each test is a self-contained
# Qt Test executable that links the minimum subset of sources it needs
# (no linking against the full FinceptTerminal target — keeps tests fast).

find_package(Qt6 ${FINCEPT_QT_VERSION} EXACT REQUIRED COMPONENTS Gui Test)

enable_testing()

# ── DataHub tests ────────────────────────────────────────────────────────────
# Links against the datahub sources and the Logger (a direct dependency).

add_executable(test_datahub
    datahub/test_datahub.cpp
    ../src/datahub/DataHub.cpp
    ../src/core/logging/Logger.cpp
)

target_include_directories(test_datahub PRIVATE
    ../src
)

target_link_libraries(test_datahub PRIVATE
    Qt6::Core
    Qt6::Test
)

# Tests shouldn't need the app's unity build settings.
set_target_properties(test_datahub PROPERTIES
    UNITY_BUILD OFF
    AUTOMOC ON
)

add_test(NAME datahub COMMAND test_datahub)

# ── VN30F Settlement tests ────────────────────────────────────────────────────
# Header-only module — only Qt6::Core is needed.

add_executable(test_vn30f_settlement
    vn30f/test_vn30f_settlement.cpp
)

target_include_directories(test_vn30f_settlement PRIVATE
    ../src
)

target_link_libraries(test_vn30f_settlement PRIVATE
    Qt6::Core
    Qt6::Test
)

set_target_properties(test_vn30f_settlement PROPERTIES
    UNITY_BUILD OFF
    AUTOMOC ON
)

add_test(NAME vn30f_settlement COMMAND test_vn30f_settlement)

# ── VN30F Risk Manager tests ──────────────────────────────────────────────────
# Needs the Logger and the VN30FRiskManager implementation unit.

add_executable(test_vn30f_risk_manager
    vn30f/test_vn30f_risk_manager.cpp
    ../src/services/vn30f/VN30FRiskManager.cpp
    ../src/core/logging/Logger.cpp
)

target_include_directories(test_vn30f_risk_manager PRIVATE
    ../src
)

target_link_libraries(test_vn30f_risk_manager PRIVATE
    Qt6::Core
    Qt6::Test
)

set_target_properties(test_vn30f_risk_manager PROPERTIES
    UNITY_BUILD OFF
    AUTOMOC ON
)

add_test(NAME vn30f_risk_manager COMMAND test_vn30f_risk_manager)

# ── VN30F Backtest config tests ───────────────────────────────────────────────
# Header-only module; BacktestingTypes.h pulls in QColor → Qt6::Gui required.

add_executable(test_vn30f_backtest
    vn30f/test_vn30f_backtest.cpp
)

target_include_directories(test_vn30f_backtest PRIVATE
    ../src
)

target_link_libraries(test_vn30f_backtest PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Test
)

set_target_properties(test_vn30f_backtest PROPERTIES
    UNITY_BUILD OFF
    AUTOMOC ON
)

add_test(NAME vn30f_backtest COMMAND test_vn30f_backtest)

# ── Trading Types tests ───────────────────────────────────────────────────────
# Header-only module — only Qt6::Core is needed.

add_executable(test_trading_types
    trading/test_trading_types.cpp
)

target_include_directories(test_trading_types PRIVATE
    ../src
)

target_link_libraries(test_trading_types PRIVATE
    Qt6::Core
    Qt6::Test
)

set_target_properties(test_trading_types PROPERTIES
    UNITY_BUILD OFF
    AUTOMOC ON
)

add_test(NAME trading_types COMMAND test_trading_types)
