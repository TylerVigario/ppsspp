// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

// OREFileBlockDevice: every ORE in PPSSPP_ORE_TEST_DIR, read block by block, against the ISO of
// its name beside it. Skipped when it isn't set, or without smelt.

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "Common/File/DirListing.h"
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"
#include "Core/FileSystems/BlockDevices.h"
#include "Core/Loaders.h"

#include "UnitTest.h"

bool TestOREBlockDevice() {
#ifndef USE_SMELT
	printf("ORE: built without smelt, skipped\n");
	return true;
#else
	const char *dir = getenv("PPSSPP_ORE_TEST_DIR");
	if (!dir || !*dir) {
		printf("ORE: PPSSPP_ORE_TEST_DIR not set, skipped\n");
		return true;
	}
	std::vector<File::FileInfo> files;
	File::GetFilesInDir(Path(dir), &files, "ore");

	int tested = 0;
	for (const auto &file : files) {
		// The filter passes folders, such as a store's .ore-store on Windows, where a dot doesn't
		// hide it, and files with no extension
		if (file.isDirectory || file.fullName.GetFileExtension() != ".ore") {
			continue;
		}
		const Path ore = file.fullName;
		const Path iso = ore.WithReplacedExtension(".iso");
		std::unique_ptr<FileLoader> oreLoader(ConstructFileLoader(ore));
		std::unique_ptr<FileLoader> isoLoader(ConstructFileLoader(iso));
		EXPECT_TRUE(isoLoader->Exists());

		std::string error;
		EXPECT_EQ_INT((int)Identify_File(oreLoader.get(), &error), (int)IdentifiedFileType::PSP_ISO);

		std::unique_ptr<BlockDevice> oreDevice(ConstructBlockDevice(oreLoader.get(), &error));
		EXPECT_TRUE(oreDevice != nullptr);
		EXPECT_TRUE(dynamic_cast<OREFileBlockDevice *>(oreDevice.get()) != nullptr);
		std::unique_ptr<BlockDevice> isoDevice(ConstructBlockDevice(isoLoader.get(), &error));
		EXPECT_TRUE(isoDevice != nullptr);

		const u32 blocks = isoDevice->GetNumBlocks();
		EXPECT_EQ_INT(oreDevice->GetNumBlocks(), blocks);
		EXPECT_TRUE(oreDevice->IsDisc());

		// Every block, read in runs of 1 to 64 blocks, and each run's first block alone again
		const int maxRun = 64;
		std::vector<u8> want(maxRun * 2048), got(maxRun * 2048), single(2048);
		u32 block = 0;
		int run = 1;
		while (block < blocks) {
			const int count = (int)std::min<u32>(run, blocks - block);
			EXPECT_TRUE(isoDevice->ReadBlocks(block, count, want.data()));
			EXPECT_TRUE(oreDevice->ReadBlocks(block, count, got.data()));
			if (memcmp(want.data(), got.data(), (size_t)count * 2048) != 0) {
				printf("ORE: %s differs in blocks %u to %u\n", ore.c_str(), block, block + count - 1);
				return false;
			}
			EXPECT_TRUE(oreDevice->ReadBlock((int)block, single.data()));
			EXPECT_TRUE(memcmp(single.data(), want.data(), 2048) == 0);
			block += count;
			run = run % maxRun + 1;
		}

		// Past the end: zeros, and a failure, with what lies inside still read
		memset(got.data(), 0xAA, 3 * 2048);
		EXPECT_FALSE(oreDevice->ReadBlocks(blocks - 1, 3, got.data()));
		EXPECT_TRUE(isoDevice->ReadBlocks(blocks - 1, 1, want.data()));
		EXPECT_TRUE(memcmp(got.data(), want.data(), 2048) == 0);
		for (int i = 2048; i < 3 * 2048; i++) {
			EXPECT_EQ_INT(got[i], 0);
		}
		EXPECT_FALSE(oreDevice->ReadBlock((int)blocks, single.data()));

		printf("ORE: %s, %u blocks, every one the ISO's\n", ore.GetFilename().c_str(), blocks);
		tested++;
	}
	EXPECT_TRUE(tested > 0);
	return true;
#endif
}
