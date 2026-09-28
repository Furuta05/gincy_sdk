# Проверки RC1

- test_sdk.py, test_v3.py: Python contracts, signed packages, native decrypt interoperability, tampering, rollback файлов, installer architecture, HTTP и mailbox security.
- runtime.lua: прежние 41 публичные проверки с mocks GLua.
- runtime_v3.lua: dependency/lifecycle/schema/network/limits/rollback регрессии с mocks GLua.
- acceptance.lua: ModernHUD без PostgreSQL/RPG и arbitrary QuantumFishing types.
- management.lua: настоящий Execute backend поверх тестовых file/JSON adapters.
- pool_test.cpp: native nonblocking libpq и bounded pool. С GINCY_TEST_PORT включаются реальные SQL/transaction/rollback assertions; без переменной проверяется connection refusal.
- package_decrypt.cpp: RSA/AES decrypt roundtrip и повреждённый tag.
- webui_test.py: настоящий браузер и HTTP gateway с явно тестовым runtime fixture; не заменяет SRCDS acceptance.
- docs_test.py: ссылки, актуальные разделы и воспроизводимость offline docs.

Переменные GINCY_PACKAGE_VERIFY и GINCY_PACKAGE_DECRYPT подключают собранные native executables к Python tests. CI PostgreSQL использует PostgreSQL 16, не PGlite. Существующие database.mjs/native_socket.mjs — старые PGlite тесты, они не служат подтверждением реального PostgreSQL integration. Фактически выполненные проверки перечислены отдельно в RELEASE-VALIDATION.md.
