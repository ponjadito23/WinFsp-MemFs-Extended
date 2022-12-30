#include <cassert>

#include "memfs-interface.h"

namespace Memfs::Interface {
	static NTSTATUS GetFileInfo(FSP_FILE_SYSTEM* fileSystem, PVOID fileNode0, FSP_FSCTL_FILE_INFO* fileInfo) {
		const FileNode* fileNode = GetFileNode(fileNode0);
		fileNode->CopyFileInfo(fileInfo);
		return STATUS_SUCCESS;
	}

	static NTSTATUS SetBasicInfo(FSP_FILE_SYSTEM* fileSystem,
	                             PVOID fileNode0, UINT32 fileAttributes,
	                             UINT64 creationTime, UINT64 lastAccessTime, UINT64 lastWriteTime, UINT64 changeTime, FSP_FSCTL_FILE_INFO* fileInfo) {
		FileNode* fileNode = GetFileNode(fileNode0);
		std::shared_ptr<FileNode> mainFileNodeShared;

		if (!fileNode->IsMainNode()) {
			mainFileNodeShared = fileNode->GetMainNode().lock();
			fileNode = mainFileNodeShared.get();
		}

		if (INVALID_FILE_ATTRIBUTES != fileAttributes) {
			fileNode->fileInfo.FileAttributes = fileAttributes;
		}
		if (0 != creationTime) {
			fileNode->fileInfo.CreationTime = creationTime;
		}
		if (0 != lastAccessTime) {
			fileNode->fileInfo.LastAccessTime = lastAccessTime;
		}
		if (0 != lastWriteTime) {
			fileNode->fileInfo.LastWriteTime = lastWriteTime;
		}
		if (0 != changeTime) {
			fileNode->fileInfo.ChangeTime = changeTime;
		}

		fileNode->CopyFileInfo(fileInfo);
		return STATUS_SUCCESS;
	}

	static NTSTATUS SetFileSize(FSP_FILE_SYSTEM* fileSystem,
	                            PVOID fileNode0, UINT64 newSize, BOOLEAN setAllocationSize,
	                            FSP_FSCTL_FILE_INFO* fileInfo) {
		FileNode* fileNode = GetFileNode(fileNode0);

		const NTSTATUS result = CompatSetFileSizeInternal(fileSystem, fileNode, newSize, setAllocationSize);
		if (!NT_SUCCESS(result))
			return result;

		fileNode->CopyFileInfo(fileInfo);
		return STATUS_SUCCESS;
	}

	static NTSTATUS CanDelete(FSP_FILE_SYSTEM* fileSystem, PVOID fileNode0, PWSTR fileName) {
		MemFs* memfs = GetMemFs(fileSystem);
		const FileNode* fileNode = GetFileNode(fileNode0);

		if (memfs->HasChild(*fileNode)) {
			return STATUS_DIRECTORY_NOT_EMPTY;
		}

		return STATUS_SUCCESS;
	}

	static NTSTATUS Rename(FSP_FILE_SYSTEM* fileSystem, PVOID fileNode0,
	                       PWSTR fileName, PWSTR newFileName, BOOLEAN replaceIfExists) {
		MemFs* memfs = GetMemFs(fileSystem);
		FileNode* fileNode = GetFileNode(fileNode0);

		const auto newFileNodeOpt = memfs->FindFile(newFileName);
		if (newFileNodeOpt.has_value() && fileNode != &newFileNodeOpt.value()) {
			const FileNode& newFileNode = newFileNodeOpt.value();
			if (!replaceIfExists) {
				return STATUS_OBJECT_NAME_COLLISION;
			}

			if (newFileNode.fileInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				return STATUS_ACCESS_DENIED;
			}
		}

		// Old length
		ULONG fileNameLen = (ULONG)fileNode->fileName.length();
		ULONG newFileNameLen = (ULONG)wcslen(newFileName);

		// Check for max path
		const auto descendants = memfs->EnumerateDescendants(*fileNode, true);
		for (const FileNode* descendant : descendants) {
			if (MEMFS_MAX_PATH <= descendant->fileName.length() - fileNameLen + newFileNameLen) {
				return STATUS_OBJECT_NAME_INVALID;
			}
		}

		if (newFileNodeOpt.has_value()) {
			FileNode& newFileNode = newFileNodeOpt.value();

			newFileNode.Reference();
			memfs->RemoveNode(newFileNode);
			newFileNode.Dereference();
		}

		// Rename descendants
		for (FileNode* descendant : descendants) {
			memfs->RemoveNode(*descendant, false);

			descendant->fileName = newFileName + descendant->fileName.substr(fileNameLen);

			const auto [result,_] = memfs->InsertNode(std::move(*descendant));
			if (!NT_SUCCESS(result)) {
				FspDebugLog(__FUNCTION__ ": cannot insert into FileNodeMap; aborting\n");
				abort();
			}
		}

		return STATUS_SUCCESS;
	}
}