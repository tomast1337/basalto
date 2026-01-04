import subprocess
import pytest
import os

# --- GLOBAL SETUP (Run once) ---
# We still keep this to avoid rebuilding the compiler 50 times.

@pytest.fixture(scope="session", autouse=True)
def setup_compiler():
    """Ensures Basalto is built before any tests run."""
    # 1. Build the build tool (nob)
    if not os.path.exists("./nob"):
        subprocess.run(["cc", "-o", "nob", "nob.c"], check=True)

    # 2. Build the compiler
    # Assuming ./nob builds ./build/basalto
    subprocess.run(["./nob"], check=True)
    yield

# --- INDIVIDUAL TESTS (Full Control) ---


def test_hello_world():
    compile_cmd = ["./build/basalto", "examples/0-hello-world.bso"]
    proc_compile = subprocess.run(compile_cmd, capture_output=True, text=True)

    if proc_compile.returncode != 0:
        pytest.fail(f"Compilation failed: {proc_compile.stderr}")


    run_cmd = ["./OlaMundo"]
    proc_run = subprocess.run(run_cmd, capture_output=True, text=True)

    assert proc_run.returncode == 0
    assert "Ola, Mundo!\nOlá, Mundo!\n" in proc_run.stdout

def test_primitives_variables():
    compile_cmd = ["./build/basalto", "examples/1-primitives-variables.bso"]
    proc_compile = subprocess.run(compile_cmd, capture_output=True, text=True)
    

    if proc_compile.returncode != 0:
        pytest.fail(f"Compilation failed: {proc_compile.stderr}")

    run_cmd = ["./TiposPrimitivos"]
    proc_run = subprocess.run(run_cmd, capture_output=True, text=True)

    # 3. Assertions
    assert proc_run.returncode == 0
    assert "Nome = Nicolas Vyčas Nery" in proc_run.stdout
    assert "Letra = A" in proc_run.stdout
    assert "Valor64 = 2147483647" in proc_run.stdout
    assert "Valor32 = 2147483647" in proc_run.stdout
    assert "Valor16 = 32767" in proc_run.stdout
    assert "Valor8 = 127" in proc_run.stdout
    assert "Valor_arq = 0" in proc_run.stdout
    assert "N_u32 = 2147483647" in proc_run.stdout
    assert "N_u64 = 2147483647" in proc_run.stdout
    assert "N_u16 = 65535" in proc_run.stdout
    assert "N_arq = 1024" in proc_run.stdout
    assert "Tam = 100" in proc_run.stdout
    assert "B = 255" in proc_run.stdout
    assert "R32 = 3.14" in proc_run.stdout
    assert "R64 = 3.1415930000" in proc_run.stdout
    assert "R_ext = 0.00000000000000000000" in proc_run.stdout
    assert "Ativo = 1" in proc_run.stdout
    assert "Ptr = 0" in proc_run.stdout
    assert "V = 0" in proc_run.stdout
    assert "I8 = -128" in proc_run.stdout
    assert "I16 = -32768" in proc_run.stdout
    assert "I32 = -2147483647" in proc_run.stdout
    assert "I64 = -2147483647" in proc_run.stdout
    assert "N16 = 65535" in proc_run.stdout
    assert "N32 = 2147483647" in proc_run.stdout
    assert "N64 = 2147483647" in proc_run.stdout
    assert "F32 = 1.500000" in proc_run.stdout
    assert "F64 = 2.718280" in proc_run.stdout
    #assert "F_ext = 485167206" in proc_run.stdout
    assert "Bl = 0" in proc_run.stdout


def test_program_with_input():
    # Example: A program that asks for a name and greets the user

    # 1. Compile
    subprocess.run(["./build/basalto", "examples/input-test.bso"], check=True)

    # 2. Run WITH INPUT
    # This specific test needs to feed "Nicolas" into stdin
    proc_run = subprocess.run(
        ["./InputTest"],
        input="Nicolas\n",  # <--- Unique requirement here
        capture_output=True,
        text=True
    )

    assert "Hello, Nicolas" in proc_run.stdout
