/*******************************************************************************
 * Copyright (c) 2021, 2021 IBM Corp. and others
 *
 * This program and the accompanying materials are made available under
 * the terms of the Eclipse Public License 2.0 which accompanies this
 * distribution and is available at https://www.eclipse.org/legal/epl-2.0/
 * or the Apache License, Version 2.0 which accompanies this distribution and
 * is available at https://www.apache.org/licenses/LICENSE-2.0.
 *
 * This Source Code may also be made available under the following
 * Secondary Licenses when the conditions for such availability set
 * forth in the Eclipse Public License, v. 2.0 are satisfied: GNU
 * General Public License, version 2 with the GNU Classpath
 * Exception [1] and GNU General Public License, version 2 with the
 * OpenJDK Assembly Exception [2].
 *
 * [1] https://www.gnu.org/software/classpath/license.html
 * [2] http://openjdk.java.net/legal/assembly-exception.html
 *
 * SPDX-License-Identifier: EPL-2.0 OR Apache-2.0 OR GPL-2.0 WITH Classpath-exception-2.0 OR LicenseRef-GPL-2.0 WITH Assembly-exception
 *******************************************************************************/
#ifndef JITSERVER_ROMCLASS_CACHE_H
#define JITSERVER_ROMCLASS_CACHE_H

#include "env/TRMemory.hpp"
#include "env/PersistentCollections.hpp"
#include "infra/Monitor.hpp"
#include "runtime/JITServerROMClassHash.hpp"


// Stores a single copy of each distinct ROMClass that is shared by multiple
// client sessions in order to reduce JITServer memory usage.
class JITServerSharedROMClassCache
   {
public:
   TR_PERSISTENT_ALLOC(TR_Memory::ROMClass)

   JITServerSharedROMClassCache();
   ~JITServerSharedROMClassCache();

   J9ROMClass *getOrCreate(const J9ROMClass *packedROMClass);
   void release(J9ROMClass *romClass);

private:
   struct Entry;

   TR_PersistentMemory *const _persistentMemory;
   // To avoid comparing the ROMClass contents inside a critical section when
   // inserting a new entry (which would increase lock contention), we instead
   // use a hash of the contents as the key. The hash is computed outside of
   // the critical section, and key hashing and comparison are very quick.
   PersistentUnorderedMap<JITServerROMClassHash, Entry *> _map;
   TR::Monitor *const _monitor;
   };


#endif /* JITSERVER_ROMCLASS_CACHE_H */