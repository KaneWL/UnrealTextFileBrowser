// Copyright (C) 2026 Kane. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "UObject/StrongObjectPtr.h"

class UContentBrowserFileDataSource;
class UToolMenu;
class SDockTab;
class STextFileEditor;

class FTextFileBrowserModule : public IModuleInterface
{
public:
	static FTextFileBrowserModule& Get();

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** 用内置文本编辑器打开文件；已打开则激活对应标签页 */
	void OpenTextFile(const FString& InFilename);

private:
	void RegisterDataSource();
	void UnregisterDataSource();
	void ApplyMounts();
	void HandleSettingsChanged();
	void HandlePreExit();

	static TArray<FString> CollectExtensions(const TArray<FString>& InRaw);
	TMap<FString, FString> CollectDesiredMounts() const;

	void PopulateFileContextMenu(UToolMenu* InMenu);

	TStrongObjectPtr<UContentBrowserFileDataSource> DataSource;
	/** 当前已挂载：挂载路径 -> 磁盘路径 */
	TMap<FString, FString> CurrentMounts;
	/** 创建数据源时使用的扩展名配置，用于判断是否需要重建 */
	TArray<FString> RegisteredExtensions;
	TArray<FString> RegisteredCreatable;
	int32 RegisteredMaxEditableKB = 0;

	TMap<FString, TWeakPtr<SDockTab>> OpenTabs;
	FDelegateHandle SettingsChangedHandle;
	FDelegateHandle PreExitHandle;
	bool bTornDown = false;
};
