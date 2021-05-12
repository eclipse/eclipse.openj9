<!--
Copyright (c) 2021, 2021 IBM Corp. and others

This program and the accompanying materials are made available under
the terms of the Eclipse Public License 2.0 which accompanies this
distribution and is available at https://www.eclipse.org/legal/epl-2.0/
or the Apache License, Version 2.0 which accompanies this distribution and
is available at https://www.apache.org/licenses/LICENSE-2.0.

This Source Code may also be made available under the following
Secondary Licenses when the conditions for such availability set
forth in the Eclipse Public License, v. 2.0 are satisfied: GNU
General Public License, version 2 with the GNU Classpath
Exception [1] and GNU General Public License, version 2 with the
OpenJDK Assembly Exception [2].

[1] https://www.gnu.org/software/classpath/license.html
[2] http://openjdk.java.net/legal/assembly-exception.html

SPDX-License-Identifier: EPL-2.0 OR Apache-2.0 OR GPL-2.0 WITH Classpath-exception-2.0 OR LicenseRef-GPL-2.0 WITH Assembly-exception
-->

# Writing Java code for DDR

Data structures used by an active OpenJ9 JVM are described in C/C++ header files. Information
about the fields of those data structures, their names, types and offsets are used by compilers
to produce native code.

That native code only needs to deal with a single version of those data structures - the
one based on the source code at build time. DDR code, on the other hand, is used to navigate
historical versions of data structures captured in system dumps from a VM built some time in
the past. Historical versions may differ from the latest source code in a number of ways.

Consider this structure declared in `j9nonbuilder.h`:

```
struct J9ModronThreadLocalHeap {
    U_8 *heapBase;
    U_8 *realHeapTop;
    UDATA objectFlags;
    UDATA refreshSize;
    void *memorySubSpace;
    void *memoryPool;
};
```

Previously, a Java source file would be generated from that using this pattern:

```
@com.ibm.j9ddr.GeneratedPointerClass(structureClass=J9ModronThreadLocalHeap.class)
public class J9ModronThreadLocalHeapPointer extends StructurePointer {

    // [other features omitted]

    // U8* realHeapTop
    @com.ibm.j9ddr.GeneratedFieldAccessor(offsetFieldName="_realHeapTopOffset_", declaredType="U8*")
    public U8Pointer realHeapTop() throws CorruptDataException {
        return U8Pointer.cast(getPointerAtOffset(J9ModronThreadLocalHeap._realHeapTopOffset_));
    }

    // U8* realHeapTop
    public PointerPointer realHeapTopEA() throws CorruptDataException {
        return PointerPointer.cast(nonNullFieldEA(J9ModronThreadLocalHeap._realHeapTopOffset_));
    }

}
```

The field `realHeapTop` was added recently. Native code can use that field without
special considerations. On the other hand, notice that the generated `realHeapTop()` and
`realHeapTopEA()` methods may throw `NoSuchFieldError`. This is because DDR may be employed
to access a core file written by an older VM built before that field existed. In that case,
the companion class, `J9ModronThreadLocalHeap` (derived from the core file), will not have the
field `_realHeapTopOffset_` resulting in the linkage error.

The path is more direct if the pointer classes are dynamically generated at runtime: the methods
`realHeapTop()` and `realHeapTopEA()` will simply not exist, resulting in a different linkage
error, `NoSuchMethodError`.

It is also possible that there is a need to use a field in a structure that was not always
present. In that situation, the linkage error would be `NoClassDefFoundError` if the pointer
classes are being generated dynamically.

Because linkage errors are unchecked exceptions, it is easy forget to handle those errors,
with unwelcome consequences at runtime.

The solution, introduced by [#12676](https://github.com/eclipse-openj9/openj9/pull/12676),
makes explicit the set of fields that may be accessed by hand-written DDR code. There are two
kinds of such fields, depending on whether they have always been present or not (they were
either added or removed at some point). Those fields are listed in `AuxFieldInfo29.dat` along
with their types. The word `required` marks those that must have always existed, for example:

```
J9Object.clazz = required
```

while an optional field gives its assumed type:

```
J9ModronThreadLocalHeap.realHeapTop = U8*
```

Pointer classes generated in source form during the build are unaffected by this extra field
information. Those generated in binary form (i.e. in `.class` files) are limited so they only
include methods corresponding to fields mentioned in `AuxFieldInfo29.dat` to prevent inadvertent
use of a field (that has not always been present) in hand-written code.

The source pattern for optional fields was modified to throw a checked exception (`NoSuchFieldException`):

```
    // U8* realHeapTop
    @com.ibm.j9ddr.GeneratedFieldAccessor(offsetFieldName="_realHeapTopOffset_", declaredType="U8*")
    public U8Pointer realHeapTop() throws CorruptDataException, NoSuchFieldException {
        try {
            return U8Pointer.cast(getPointerAtOffset(J9ModronThreadLocalHeap._realHeapTopOffset_));
        } catch (NoClassDefFoundError | NoSuchFieldError e) {
            throw new NoSuchFieldException();
        }
    }

    // U8* realHeapTop
    public PointerPointer realHeapTopEA() throws CorruptDataException, NoSuchFieldException {
        try {
            return PointerPointer.cast(nonNullFieldEA(J9ModronThreadLocalHeap._realHeapTopOffset_));
        } catch (NoClassDefFoundError | NoSuchFieldError e) {
            throw new NoSuchFieldException();
        }
    }
```

Dynamically generated classes include either this when the field is present (in a 64-bit core):

```
    public U8Pointer realHeapTop() throws CorruptDataException, NoSuchFieldException {
        return U8Pointer.cast(getPointerAtOffset(8));
    }
```

or this when the field is absent:

```
    public U8Pointer realHeapTop() throws CorruptDataException, NoSuchFieldException {
        throw new NoSuchFieldException();
    }
```

Code that accesses optional fields must handle `NoSuchFieldException` because it is a checked
exception.

To start using a field in DDR code, an entry must be added to `AuxFieldInfo29.dat`. If the field
has always been present, then it may be marked as `required`; otherwise, specify its type and
handle the possibility of `NoSuchFieldException` where it is used.

If a field required by DDR code is removed, the word `required` (in `AuxFieldInfo29.dat`)
must be replaced by the type of the field (as previously recorded in `j9ddr.dat`) and DDR code
must be updated to deal appropriately with `NoSuchFieldException`.

A similar problem arises with the use of constants. The value of a constant may change over
time without issue: its value will be captured in the DDR blob found within core files (a
copy of the contents of `j9ddr.dat`). However, constants that have not always existed can
translate to `NoSuchFieldError` when accessed in DDR code.

The solution, introduced by [#12210](https://github.com/eclipse-openj9/openj9/pull/12210),
involves two new files: `superset-constants.dat` and `CompatibilityConstants29.dat`.

The first file, `superset-constants.dat`, lists constants that are legal to be used in DDR
code (if still defined in C/C++ source files). The other file, `CompatibilityConstants29.dat`,
names constants that might not be found in any given core file along with a value to be used
when absent. The intent is that the value can be distinguished from a normal value. For example,
zero is a sensible value for a constant representing a bit-mask, where a condition is recognized
by one or more non-zero bits corresponding to those included in the mask.

A constant absent from both files will not be available for use in hand-written DDR code (a
Java compile error will result for references to such constants).
