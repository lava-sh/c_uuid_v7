# AGENTS.md

## Общие правила

- Всегда проверяй изменённые файлы перед завершением задачи.
- Выполняй только те проверки, которые относятся к изменённым файлам.
- Если инструмент сообщает об ошибках --- обязательно исправь их.
- Повторяй проверки до тех пор, пока они не проходят без ошибок.
- Если пользователь присылает ссылку на GitHub, сразу запрашивай выполнение
  команды вне sandbox через `gh`, без лишних попыток обхода другими способами.

## Область работы

Основные файлы и директории проекта:

- `src/`
- `tests/`
- `.github/`
- `pyproject.toml`
- `.ruff.toml`
- `.rumdl.toml`
- `ty.toml`

## Установка зависимостей

- Сторонние зависимости устанавливай через:

  ``` bash
  uv pip install <package>
  ```

- Сам проект устанавливай:

  ``` bash
  uv pip install -e .
  ```

## Тесты

Запуск тестов:

``` bash
pytest
```

## Написание кода (Python)

### Требования

- Соблюдай `PEP8`

### Запрещено

- `global`
- `typing.TYPE_CHECKING` (использовать только если иначе невозможно)
- Тайпхинт `object`, если нужен не буквально любой объект Python;
  вместо этого используй конкретный тип, протокол, `Any`, `UUID`,
  `Callable` и т.д.

### Комментарии

- Не добавляй комментарии без явного запроса
- Не удаляй существующие комментарии
- Не используй временный или мусорный нейминг вроде `_optimized`, `_new`,
  `_final`, `_temp`, `_old`, `_test`, если это не действительно временный
  тестовый код
- Не придумывай без необходимости шумный screaming-case нейминг с длинными
  префиксами вроде `C_UUID_V7_PYTHON_*`; для внутренних define/macros/helper
  имён выбирай короткие и понятные имена без раздутых project-prefix цепочек,
  если это не требуется внешним API, стандартом или для предотвращения
  реального конфликта имён
- Если новая реализация становится основной, давай ей нормальное
  окончательное имя, а не имя с суффиксом про эксперимент

## Проверки

### 🐍 Python

Если изменены `.py` файлы:

``` bash
ruff check
ty check
```

Если есть ошибки:

- исправь их
- повторяй проверки до полного отсутствия ошибок

### 📝 Markdown

Если изменены `.md` файлы:

``` bash
rumdl check
```

- Исправь все ошибки

### ⚙️ CI / GitHub

Если изменена директория `.github/`:

``` bash
zizmor .github/
```

- Убедись, что CI конфигурация корректна

### 🧩 C

Если изменён C-код:

``` bash
Get-ChildItem src -Filter *.c -File | ForEach-Object { clang-format -i $_.FullName }
Get-ChildItem src -Filter *.h -File | ForEach-Object { $formatted = Get-Content $_.FullName | clang-format --assume-filename=dummy.c; Set-Content $_.FullName $formatted }
cppcheck --enable=warning,style,performance,portability --quiet src/core.c
Get-ChildItem src -Filter *.c -File | Where-Object { $_.Name -ne 'core.c' } | ForEach-Object { cppcheck --enable=warning,style,performance,portability --quiet $_.FullName }
clang-tidy src/core.c --
Get-ChildItem src -Filter *.c -File | Where-Object { $_.Name -ne 'core.c' } | ForEach-Object { clang-tidy $_.FullName -- }
$python_include = & .\.venv\Scripts\python.exe -c "import sysconfig; print(sysconfig.get_paths()['include'])"
clang-tidy --extra-arg="-I$python_include" src/core.c --
Get-ChildItem src -Filter *.c -File | Where-Object { $_.Name -ne 'core.c' } | ForEach-Object { clang-tidy --extra-arg="-I$python_include" $_.FullName -- }
```

- Если `clang-tidy src/core.c --` не находит `Python.h`, бери
  include-путь из `.venv\Scripts\python.exe` и передавай его через
  `--extra-arg="-I..."`
- Для файлов из `src/*.c`, кроме `src/core.c`, используй тот же include-path,
  если `clang-tidy` не находит `Python.h`
- Удаляй мёртвый код после завершения экспериментов и бенчмарков
- Не оставляй внутренние benchmark/debug entrypoint'ы в `PyMethodDef`,
  если они не нужны публичному API
- Не оставляй в C-коде временный нейминг вроде `_optimized`, `_new`,
  `_final`, `_temp`, `_old`, если реализация уже выбрана как основная
- Если логика повторяется и её можно изолированно проверить, выноси её
  в отдельную `static`-функцию
- Для UUIDv7 не нарушай `RFC 9562`: timestamp, version, variant
  и monotonic ordering должны сохраняться
- Исправляй реальные предупреждения `cppcheck` и `clang-tidy`,
  относящиеся к корректности, portability и performance
- Рекомендательные предупреждения `clang-tidy`/`cppcheck` для callback'ов
  CPython API или общие замечания про `memcpy` в фиксированных буферах
  можно не считать обязательными, если они не указывают на реальную ошибку

## Критерий завершения

Задача считается завершённой только если:

- `ruff check .` проходит без ошибок (если применимо)
- `ty check` проходит без ошибок (если применимо)
- `rumdl check` проходит без ошибок (если применимо)
- `zizmor .github/` проходит без ошибок (если применимо)
- C-код отформатирован (если применимо)
- `cppcheck --enable=warning,style,performance,portability --quiet
  src/core.c` отработал без существенных замечаний
  (если применимо)
- `cppcheck --enable=warning,style,performance,portability --quiet
  src/*.c` для файлов, кроме `src/core.c`, отработал без существенных замечаний
  (если применимо)
- `clang-tidy src/core.c --` отработал без существенных
  замечаний или явно требует include-path для `Python.h`
  (если применимо)
- `clang-tidy src/*.c` для файлов, кроме `src/core.c`, отработал без существенных
  замечаний или явно требует include-path для `Python.h`
  (если применимо)
- `clang-tidy --extra-arg="-I$python_include" src/core.c --`
  отработал без существенных замечаний (если применимо)
- `clang-tidy --extra-arg="-I$python_include" src/*.c` для файлов, кроме
  `src/core.c`, отработал без существенных замечаний (если применимо)
- тесты проходят (`pytest`)

## Бенчмарки

- Для бенчмарков всегда используй отдельную директорию `.tmp_bench/`.
- Внутри `.tmp_bench/` создавай отдельные виртуальные окружения через:

  ``` bash
  uv venv <название> --seed
  ```

- Сначала создай базовое окружение с именем `main` для проекта без новых изменений.
- Установи в окружение `main` исходную версию проекта и используй его как точку сравнения.
- После этого вноси изменения в код.
- Перед замером новой версии обязательно прогони тесты и убедись, что они проходят.
- Затем создай отдельное окружение для новой версии проекта внутри `.tmp_bench/`.
- Для окружений с кандидатами можно использовать любые понятные имена.
- Установи в это окружение уже обновлённую версию проекта.
- Только после этого запускай:

  ``` bash
  python benchmark/self_bench.py
  ```
