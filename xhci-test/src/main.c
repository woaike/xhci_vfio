// XHCI test runner — enum once, run test cases dynamically.
//
// Usage:
//   ./build/main --enum           Enumerate devices, save state
//   ./build/main --test <test.so> Load state, run test case

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include "xhci.h"

#define DEFAULT_PCI   "0000:05:00.1"
#define STATE_FILE    "xhci_state.dat"

/*

# 默认（仅错误）
bash scripts/run_test.sh

# 详细日志：看命令和槽位
XHCI_DEBUG=1 bash scripts/run_test.sh

# 更详细：端口、ring
XHCI_DEBUG=2 bash scripts/run_test.sh

# 最详细：每个 TRB 的完整参数
XHCI_DEBUG=3 bash scripts/run_test.sh
*/


static const char *
get_pci_bdf(void)
{
    const char *env = getenv("XHCI_PCI");
    return env ? env : DEFAULT_PCI;
}

static const char *
get_state_file(void)
{
    const char *env = getenv("XHCI_STATE_FILE");
    return env ? env : STATE_FILE;
}

/****************************************************************
// Enum mode
 ****************************************************************/

static int
run_enum(void)
{
    const char *pci_bdf = get_pci_bdf();
    const char *state_file = get_state_file();

    printf("=== XHCI State Builder ===\n");
    printf("Device: %s\n", pci_bdf);
    printf("Output: %s\n\n", state_file);

    fprintf(stderr, "[MAIN] Opening device %s...\n", pci_bdf);

    void *handle = xhci_open(pci_bdf);
    if (!handle) {
        fprintf(stderr, "[MAIN] xhci_open(%s) FAILED\n", pci_bdf);
        fprintf(stderr, "Failed to open XHCI device %s\n", pci_bdf);
        return 1;
    }
    printf("[MAIN] xhci_open OK\n");

    struct xhci_state *state = NULL;
    if (xhci_state_enumerate(handle, &state) < 0) {
        fprintf(stderr, "[MAIN] xhci_state_enumerate FAILED\n");
        fprintf(stderr, "Enumeration failed\n");
        xhci_close(handle);
        return 1;
    }
    printf("[MAIN] enumeration OK, n_slots=%d\n", state->n_slots);

    printf("\nSaving state to %s...\n", state_file);
    if (xhci_state_save(handle, state, state_file) < 0) {
        fprintf(stderr, "[MAIN] xhci_state_save FAILED\n");
        fprintf(stderr, "Failed to save state\n");
        xhci_state_free(state);
        xhci_close(handle);
        return 1;
    }
    printf("[MAIN] state saved OK\n");

    printf("Saved state for %d device(s)\n", state->n_slots);

    xhci_state_free(state);
    xhci_close(handle);
    return 0;
}

/****************************************************************
// Test mode
 ****************************************************************/

static int
run_test(const char *so_path)
{
    const char *pci_bdf = get_pci_bdf();
    const char *state_file = get_state_file();

    printf("=== XHCI Test Runner ===\n");
    printf("Device: %s\n", pci_bdf);
    printf("State file: %s\n", state_file);
    printf("Test case: %s\n\n", so_path);

    void *handle = xhci_open(pci_bdf);
    if (!handle) {
        fprintf(stderr, "Failed to open XHCI device %s\n", pci_bdf);
        return 1;
    }

    if (xhci_full_init(handle) < 0) {
        fprintf(stderr, "Controller init failed\n");
        xhci_close(handle);
        return 1;
    }
    printf("Controller initialized\n");

    struct xhci_state *state = xhci_state_load(state_file);
    if (!state) {
        fprintf(stderr, "Failed to load state from %s\n", state_file);
        xhci_close(handle);
        return 1;
    }

    int failures = xhci_state_restore(handle, state);
    if (failures < 0) {
        fprintf(stderr, "State restore failed entirely\n");
        xhci_state_free(state);
        xhci_close(handle);
        return 1;
    }
    if (failures > 0)
        fprintf(stderr, "Warning: %d slot(s) failed to restore\n", failures);

    printf("State restored (%d/%d slots)\n\n",
           state->n_slots - failures, state->n_slots);

    void *test_lib = dlopen(so_path, RTLD_NOW);
    if (!test_lib) {
        fprintf(stderr, "Failed to load test case: %s\n", dlerror());
        xhci_state_free(state);
        xhci_close(handle);
        return 1;
    }

    int (*test_main)(void *, struct xhci_state *) =
        (int (*)(void *, struct xhci_state *))dlsym(test_lib, "test_main");
    if (!test_main) {
        fprintf(stderr, "Failed to find test_main in %s: %s\n", so_path, dlerror());
        dlclose(test_lib);
        xhci_state_free(state);
        xhci_close(handle);
        return 1;
    }

    printf("--- Running test case ---\n");
    int result = test_main(handle, state);
    printf("--- Test completed: %s ---\n", result == 0 ? "PASS" : "FAIL");

    dlclose(test_lib);
    xhci_state_free(state);
    xhci_close(handle);
    return result;
}

static void
usage(const char *prog)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s --enum              Enumerate devices and save state\n"
            "  %s --test <test.so>    Load state and run test case\n"
            "\n"
            "Environment:\n"
            "  XHCI_PCI         PCI BDF (default: %s)\n"
            "  XHCI_STATE_FILE  State file path (default: %s)\n",
            prog, prog, DEFAULT_PCI, STATE_FILE);
}

int
main(int argc, char **argv)
{
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "--enum") == 0)
        return run_enum();
    else if (strcmp(argv[1], "--test") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: --test requires a .so path\n\n");
            usage(argv[0]);
            return 1;
        }
        return run_test(argv[2]);
    } else if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        usage(argv[0]);
        return 0;
    } else {
        fprintf(stderr, "Unknown argument: %s\n\n", argv[1]);
        usage(argv[0]);
        return 1;
    }
}
