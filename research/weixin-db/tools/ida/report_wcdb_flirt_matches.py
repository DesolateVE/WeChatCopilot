"""Report WCDB-looking functions after a FLIRT signature is applied in IDA."""

import ida_auto
import ida_funcs
import ida_name
import idautils


def main():
    ida_auto.auto_wait()

    library_functions = []
    wcdb_named_functions = []
    for address in idautils.Functions():
        flags = ida_funcs.get_func_flags(address)
        name = ida_name.get_name(address)
        if flags & ida_funcs.FUNC_LIB:
            library_functions.append((address, name))
        if "WCDB" in name or "wcdb" in name:
            wcdb_named_functions.append((address, name))

    print("[WCDB FLIRT] IDA library functions:", len(library_functions))
    print("[WCDB FLIRT] Names containing WCDB/wcdb:", len(wcdb_named_functions))
    for address, name in wcdb_named_functions[:100]:
        print(f"  {address:#x} {name}")
    if len(wcdb_named_functions) > 100:
        print(f"  ... {len(wcdb_named_functions) - 100} more")


if __name__ == "__main__":
    main()
