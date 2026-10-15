// Copyright (C) 2026 Kane. All Rights Reserved.

#include "TextFileBrowserModule.h"

#include "STextFileEditor.h"
#include "TextFileBrowserSettings.h"

#include "ContentBrowserDataMenuContexts.h"
#include "ContentBrowserDataSubsystem.h"
#include "ContentBrowserFileDataCore.h"
#include "ContentBrowserFileDataPayload.h"
#include "ContentBrowserFileDataSource.h"
#include "IContentBrowserDataModule.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Settings/EditorStyleSettings.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "TextFileBrowser"

DEFINE_LOG_CATEGORY_STATIC(LogTextFileBrowser, Log, All);

namespace
{
	const FName DataSourceBaseName(TEXT("TextFileData"));

	FString NormalizeExtension(const FString& InExt)
	{
		FString Ext = InExt.TrimStartAndEnd();
		Ext.RemoveFromStart(TEXT("."));
		return Ext.ToLower();
	}

	FString ResolveDiskPath(const FString& InPath)
	{
		if (FPaths::IsRelative(InPath))
		{
			return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / InPath);
		}
		return FPaths::ConvertRelativePathToFull(InPath);
	}

	FString StripTrailingSlash(FString InPath)
	{
		while (InPath.Len() > 1 && (InPath.EndsWith(TEXT("/")) || InPath.EndsWith(TEXT("\\"))))
		{
			InPath.LeftChopInline(1);
		}
		return InPath;
	}

	/** 把用户填的任意形式（反斜杠 / 相对路径 / 空）规范成 /Root/Sub 形式的挂载路径 */
	FString NormalizeMountPath(FString InMountPath, const FString& InDiskPath)
	{
		InMountPath.ReplaceInline(TEXT("\\"), TEXT("/"));
		InMountPath.TrimStartAndEndInline();

		bool bStripped = true;
		while (bStripped)
		{
			bStripped = InMountPath.RemoveFromStart(TEXT("../")) || InMountPath.RemoveFromStart(TEXT("./")) || InMountPath.RemoveFromStart(TEXT("/"));
		}
		while (InMountPath.ReplaceInline(TEXT("//"), TEXT("/")) > 0) {}
		InMountPath = StripTrailingSlash(InMountPath);

		if (InMountPath.IsEmpty())
		{
			InMountPath = FPaths::GetCleanFilename(StripTrailingSlash(InDiskPath));
		}
		if (InMountPath.IsEmpty())
		{
			return FString();
		}

		// 这些根名被引擎包路径占用，挂到同名根下会被 Content Browser 当成类/临时包路径处理
		static const TCHAR* ReservedRoots[] = { TEXT("Script"), TEXT("Temp"), TEXT("Memory"), TEXT("Config"), TEXT("Classes_Game"), TEXT("Classes_Engine") };
		int32 SlashIdx = INDEX_NONE;
		const FString Root = InMountPath.FindChar(TEXT('/'), SlashIdx) ? InMountPath.Left(SlashIdx) : InMountPath;
		for (const TCHAR* Reserved : ReservedRoots)
		{
			if (Root.Equals(Reserved, ESearchCase::IgnoreCase))
			{
				InMountPath = Root + TEXT("Files") + InMountPath.RightChop(Root.Len());
				break;
			}
		}

		return TEXT("/") + InMountPath;
	}
}

FTextFileBrowserModule& FTextFileBrowserModule::Get()
{
	return FModuleManager::LoadModuleChecked<FTextFileBrowserModule>("TextFileBrowser");
}

void FTextFileBrowserModule::StartupModule()
{
	if (!GIsEditor || IsRunningCommandlet())
	{
		return;
	}

	RegisterDataSource();

	UTextFileBrowserSettings* Settings = GetMutableDefault<UTextFileBrowserSettings>();
	SettingsChangedHandle = Settings->OnSettingsChanged.AddRaw(this, &FTextFileBrowserModule::HandleSettingsChanged);

	// 模块 ShutdownModule 在 UObject 系统销毁之后才调用，那时 DataSource 已是野指针；必须在 PreExit 阶段拆除
	PreExitHandle = FCoreDelegates::OnPreExit.AddRaw(this, &FTextFileBrowserModule::HandlePreExit);
}

void FTextFileBrowserModule::ShutdownModule()
{
	FCoreDelegates::OnPreExit.Remove(PreExitHandle);

	if (UObjectInitialized() && !IsEngineExitRequested())
	{
		HandlePreExit();
	}
	else
	{
		// 对象已随 UObject 系统一起销毁，只能丢弃引用
		DataSource.Reset();
	}
}

void FTextFileBrowserModule::HandlePreExit()
{
	if (bTornDown)
	{
		return;
	}
	bTornDown = true;

	if (UObjectInitialized())
	{
		if (UTextFileBrowserSettings* Settings = GetMutableDefault<UTextFileBrowserSettings>())
		{
			Settings->OnSettingsChanged.Remove(SettingsChangedHandle);
		}
	}

	if (UToolMenus* ToolMenus = UToolMenus::TryGet())
	{
		ToolMenus->UnregisterOwner(this);
	}

	UnregisterDataSource();
}

// ====================================================================================================
// 数据源
// ====================================================================================================

TArray<FString> FTextFileBrowserModule::CollectExtensions(const TArray<FString>& InRaw)
{
	TSet<FString> Unique;
	for (const FString& Raw : InRaw)
	{
		const FString Ext = NormalizeExtension(Raw);
		if (!Ext.IsEmpty())
		{
			Unique.Add(Ext);
		}
	}
	TArray<FString> Sorted = Unique.Array();
	Sorted.Sort();
	return Sorted;
}

TMap<FString, FString> FTextFileBrowserModule::CollectDesiredMounts() const
{
	const UTextFileBrowserSettings* Settings = GetDefault<UTextFileBrowserSettings>();
	TMap<FString, FString> Desired;

	auto Add = [&Desired](const FString& InMountPath, const FString& InDiskPath)
	{
		const FString MountPath = StripTrailingSlash(InMountPath);
		if (MountPath.Len() < 2 || !MountPath.StartsWith(TEXT("/")) || InDiskPath.IsEmpty())
		{
			return;
		}
		const FString DiskPath = StripTrailingSlash(FPaths::ConvertRelativePathToFull(InDiskPath));
		if (!FPaths::DirectoryExists(DiskPath))
		{
			UE_LOG(LogTextFileBrowser, Warning, TEXT("跳过挂载 %s：目录不存在 %s"), *MountPath, *DiskPath);
			return;
		}
		if (!Desired.Contains(MountPath))
		{
			Desired.Add(MountPath, DiskPath);
		}
	};

	if (Settings->bMountProjectContent)
	{
		Add(TEXT("/Game"), FPaths::ProjectContentDir());
	}
	if (Settings->bMountEngineContent)
	{
		Add(TEXT("/Engine"), FPaths::EngineContentDir());
	}
	if (Settings->bMountProjectPlugins)
	{
		for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPluginsWithContent())
		{
			if (Plugin->GetLoadedFrom() == EPluginLoadedFrom::Project)
			{
				Add(Plugin->GetMountedAssetPath(), Plugin->GetContentDir());
			}
		}
	}
	if (Settings->bMountProjectSource)
	{
		Add(TEXT("/ProjectSource"), FPaths::GameSourceDir());
	}
	for (const FTextFileBrowserMount& Extra : Settings->ExtraMounts)
	{
		if (Extra.DiskPath.IsEmpty())
		{
			continue;
		}
		const FString DiskPath = ResolveDiskPath(Extra.DiskPath);
		Add(NormalizeMountPath(Extra.MountPath, DiskPath), DiskPath);
	}

	return Desired;
}

void FTextFileBrowserModule::RegisterDataSource()
{
	const UTextFileBrowserSettings* Settings = GetDefault<UTextFileBrowserSettings>();

	RegisteredExtensions = CollectExtensions(Settings->TextExtensions);
	RegisteredCreatable = CollectExtensions(Settings->CreatableExtensions);
	RegisteredMaxEditableKB = Settings->MaxEditableFileSizeKB;

	if (RegisteredExtensions.Num() == 0)
	{
		UE_LOG(LogTextFileBrowser, Warning, TEXT("TextExtensions 为空，未注册文本文件数据源"));
		return;
	}

	ContentBrowserFileData::FFileConfigData Config;
	{
		ContentBrowserFileData::FDirectoryActions DirectoryActions;
		DirectoryActions.PassesFilter.BindStatic(&ContentBrowserFileData::FDefaultFileActions::ItemPassesFilter, false);
		DirectoryActions.GetAttribute.BindStatic(&ContentBrowserFileData::FDefaultFileActions::GetItemAttribute);
		Config.SetDirectoryActions(DirectoryActions);

		const int64 MaxEditableBytes = (int64)RegisteredMaxEditableKB * 1024;

		for (const FString& Ext : RegisteredExtensions)
		{
			const bool bCanCreate = RegisteredCreatable.Contains(Ext);

			ContentBrowserFileData::FFileActions FileActions;
			FileActions.TypeExtension = Ext;
			// 缩略图上显示的就是这个"类名"，直接用大写后缀
			FileActions.TypeName = FTopLevelAssetPath(TEXT("/Script/TextFileBrowser"), *Ext.ToUpper());
			FileActions.TypeDisplayName = FText::Format(LOCTEXT("TypeDisplayName", "{0} 文件"), FText::FromString(Ext.ToUpper()));
			FileActions.TypeShortDescription = FText::Format(LOCTEXT("TypeShortDesc", "{0} 文本文件"), FText::FromString(Ext.ToUpper()));
			FileActions.TypeFullDescription = FText::Format(LOCTEXT("TypeFullDesc", "扩展名为 .{0} 的文本文件，双击使用内置文本编辑器打开"), FText::FromString(Ext));
			FileActions.DefaultNewFileName = FString::Printf(TEXT("New_%s_File"), *Ext);
			FileActions.TypeColor = FColor(160, 160, 160);
			FileActions.PassesFilter.BindStatic(&ContentBrowserFileData::FDefaultFileActions::ItemPassesFilter, true);
			FileActions.GetAttribute.BindStatic(&ContentBrowserFileData::FDefaultFileActions::GetItemAttribute);
			FileActions.CanCreate.BindLambda([bCanCreate](const FName, const FString&, FText*) { return bCanCreate; });
			FileActions.Edit.BindLambda([this, MaxEditableBytes](const FName, const FString& InFilename)
			{
				if (IFileManager::Get().FileSize(*InFilename) > MaxEditableBytes)
				{
					FPlatformProcess::LaunchFileInDefaultExternalApplication(*InFilename, nullptr, ELaunchVerb::Edit);
					return true;
				}
				OpenTextFile(InFilename);
				return true;
			});
			Config.RegisterFileActions(FileActions);
		}
	}

	// 名字必须唯一：子系统按名字管理数据源，若旧对象与新对象同名，旧对象 GC 时会把新对象一并反激活
	DataSource.Reset(NewObject<UContentBrowserFileDataSource>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UContentBrowserFileDataSource::StaticClass(), DataSourceBaseName)));
	DataSource->Initialize(Config);
	CurrentMounts.Reset();
	UE_LOG(LogTextFileBrowser, Log, TEXT("已注册文本文件数据源 %s，扩展名 %d 个：%s"), *DataSource->GetName(), RegisteredExtensions.Num(), *FString::Join(RegisteredExtensions, TEXT(" ")));

	// 右键菜单名由数据源名决定，随数据源一起重新挂接
	{
		UToolMenus::UnregisterOwner(this);
		FToolMenuOwnerScoped OwnerScoped(this);
		const FName MenuName = *FString::Printf(TEXT("ContentBrowser.ItemContextMenu.%s"), *DataSource->GetName());
		if (UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(MenuName))
		{
			Menu->AddDynamicSection(TEXT("DynamicSection_TextFileBrowser"), FNewToolMenuDelegate::CreateRaw(this, &FTextFileBrowserModule::PopulateFileContextMenu));
		}
	}

	ApplyMounts();

	// 子系统只会自动激活配置里 EnabledDataSources 列出的数据源，这里必须显式激活
	if (UContentBrowserDataSubsystem* Subsystem = IContentBrowserDataModule::Get().GetSubsystem())
	{
		Subsystem->ActivateDataSource(DataSource->GetFName());
	}
	else
	{
		UE_LOG(LogTextFileBrowser, Warning, TEXT("ContentBrowserDataSubsystem 尚未创建，数据源未激活"));
	}
}

void FTextFileBrowserModule::ApplyMounts()
{
	if (!DataSource.IsValid())
	{
		return;
	}

	const TMap<FString, FString> Desired = CollectDesiredMounts();

	// 先卸载不再需要或磁盘目录变了的
	for (auto It = CurrentMounts.CreateIterator(); It; ++It)
	{
		const FString* DesiredDisk = Desired.Find(It.Key());
		if (!DesiredDisk || !DesiredDisk->Equals(It.Value(), ESearchCase::IgnoreCase))
		{
			if (DataSource->HasFileMount(*It.Key()))
			{
				DataSource->RemoveFileMount(*It.Key());
			}
			UE_LOG(LogTextFileBrowser, Log, TEXT("已卸载 %s"), *It.Key());
			It.RemoveCurrent();
		}
	}

	// 再挂载新增的
	for (const TPair<FString, FString>& Pair : Desired)
	{
		if (CurrentMounts.Contains(Pair.Key))
		{
			continue;
		}
		if (!DataSource->HasFileMount(*Pair.Key))
		{
			DataSource->AddFileMount(*Pair.Key, Pair.Value);
		}
		CurrentMounts.Add(Pair.Key, Pair.Value);
		UE_LOG(LogTextFileBrowser, Log, TEXT("已挂载 %s -> %s"), *Pair.Key, *Pair.Value);
	}
}

void FTextFileBrowserModule::UnregisterDataSource()
{
	if (DataSource.IsValid() && IsValid(DataSource.Get()))
	{
		// 先卸载挂载点，释放目录监视回调；Shutdown 本身不会做这件事
		for (const TPair<FString, FString>& Pair : CurrentMounts)
		{
			if (DataSource->HasFileMount(*Pair.Key))
			{
				DataSource->RemoveFileMount(*Pair.Key);
			}
		}
		CurrentMounts.Reset();

		DataSource->Shutdown();
	}
	DataSource.Reset();
}

void FTextFileBrowserModule::HandleSettingsChanged()
{
	const UTextFileBrowserSettings* Settings = GetDefault<UTextFileBrowserSettings>();

	// 扩展名相关的配置烘焙在 FFileConfigData 里，只能重建；仅挂载变化时增量更新即可
	const bool bNeedRecreate = !DataSource.IsValid()
		|| CollectExtensions(Settings->TextExtensions) != RegisteredExtensions
		|| CollectExtensions(Settings->CreatableExtensions) != RegisteredCreatable
		|| Settings->MaxEditableFileSizeKB != RegisteredMaxEditableKB;

	if (bNeedRecreate)
	{
		UnregisterDataSource();
		RegisterDataSource();
	}
	else
	{
		ApplyMounts();
	}
}

// ====================================================================================================
// 右键菜单
// ====================================================================================================

void FTextFileBrowserModule::PopulateFileContextMenu(UToolMenu* InMenu)
{
	const UContentBrowserDataMenuContext_FileMenu* Context = InMenu->FindContext<UContentBrowserDataMenuContext_FileMenu>();
	if (!Context || !DataSource.IsValid())
	{
		return;
	}

	TArray<FString> SelectedFiles;
	for (const FContentBrowserItem& Item : Context->SelectedItems)
	{
		if (const FContentBrowserItemData* ItemData = Item.GetPrimaryInternalItem())
		{
			if (TSharedPtr<const FContentBrowserFileItemDataPayload> Payload = ContentBrowserFileData::GetFileItemPayload(DataSource.Get(), *ItemData))
			{
				SelectedFiles.Add(Payload->GetFilename());
			}
		}
	}

	if (SelectedFiles.Num() == 0)
	{
		return;
	}

	FToolMenuSection& Section = InMenu->AddSection("TextFileBrowser", LOCTEXT("ContextSection", "文本文件"));
	Section.InsertPosition.Position = EToolMenuInsertType::First;

	Section.AddMenuEntry(
		"OpenInTextEditor",
		LOCTEXT("OpenInTextEditor", "用内置编辑器打开"),
		LOCTEXT("OpenInTextEditorTooltip", "在编辑器内的文本编辑器中打开（等同双击）"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Edit"),
		FUIAction(FExecuteAction::CreateLambda([this, SelectedFiles]()
		{
			for (const FString& File : SelectedFiles)
			{
				OpenTextFile(File);
			}
		})));

	Section.AddMenuEntry(
		"OpenInExternalEditor",
		LOCTEXT("OpenInExternalEditor", "用外部程序打开"),
		LOCTEXT("OpenInExternalEditorTooltip", "用系统默认关联程序打开"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.OpenInExternalEditor"),
		FUIAction(FExecuteAction::CreateLambda([SelectedFiles]()
		{
			for (const FString& File : SelectedFiles)
			{
				FPlatformProcess::LaunchFileInDefaultExternalApplication(*File, nullptr, ELaunchVerb::Edit);
			}
		})));

	Section.AddMenuEntry(
		"CopyFilePath",
		LOCTEXT("CopyFilePath", "复制文件路径"),
		LOCTEXT("CopyFilePathTooltip", "把完整磁盘路径复制到剪贴板"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Copy"),
		FUIAction(FExecuteAction::CreateLambda([SelectedFiles]()
		{
			FPlatformApplicationMisc::ClipboardCopy(*FString::Join(SelectedFiles, LINE_TERMINATOR));
		})));
}

// ====================================================================================================
// 打开编辑器标签页
// ====================================================================================================

void FTextFileBrowserModule::OpenTextFile(const FString& InFilename)
{
	const FString Filename = FPaths::ConvertRelativePathToFull(InFilename);
	const FString Key = Filename.ToLower();

	if (TWeakPtr<SDockTab>* Existing = OpenTabs.Find(Key))
	{
		if (TSharedPtr<SDockTab> Tab = Existing->Pin())
		{
			Tab->ActivateInParent(ETabActivationCause::SetDirectly);
			Tab->DrawAttention();
			if (TSharedPtr<SWindow> Window = Tab->GetParentWindow())
			{
				Window->BringToFront();
			}
			return;
		}
		OpenTabs.Remove(Key);
	}

	TSharedRef<STextFileEditor> Editor = SNew(STextFileEditor).Filename(Filename);
	if (!Editor->IsLoaded())
	{
		return;
	}

	TSharedRef<SDockTab> Tab = SNew(SDockTab)
		.TabRole(ETabRole::MajorTab)
		.ContentPadding(0.0f)
		.Label(TAttribute<FText>::CreateSP(&Editor.Get(), &STextFileEditor::GetTabLabel))
		.ToolTipText(FText::FromString(Filename))
		.OnCanCloseTab(SDockTab::FCanCloseTab::CreateSP(Editor, &STextFileEditor::HandleCloseRequest))
		.OnTabClosed(SDockTab::FOnTabClosedCallback::CreateLambda([this, Key](TSharedRef<SDockTab>)
		{
			OpenTabs.Remove(Key);
		}))
		[
			Editor
		];
	Tab->SetTabIcon(FAppStyle::GetBrush("Icons.Documentation"));
	Editor->SetOwnerTab(Tab);

	// 沿用资产编辑器的打开位置偏好
	FName PlaceholderId(TEXT("StandaloneToolkit"));
	TSharedPtr<FTabManager::FSearchPreference> SearchPreference;
	switch (GetDefault<UEditorStyleSettings>()->AssetEditorOpenLocation)
	{
	case EAssetEditorOpenLocation::NewWindow:
		SearchPreference = MakeShared<FTabManager::FRequireClosedTab>();
		break;
	case EAssetEditorOpenLocation::MainWindow:
		PlaceholderId = TEXT("DockedToolkit");
		SearchPreference = MakeShared<FTabManager::FLiveTabSearch>(TEXT("LevelEditor"));
		break;
	case EAssetEditorOpenLocation::ContentBrowser:
		PlaceholderId = TEXT("DockedToolkit");
		SearchPreference = MakeShared<FTabManager::FLiveTabSearch>(TEXT("ContentBrowserTab1"));
		break;
	case EAssetEditorOpenLocation::LastDockedWindowOrNewWindow:
		SearchPreference = MakeShared<FTabManager::FLastMajorOrNomadTab>(NAME_None);
		break;
	case EAssetEditorOpenLocation::LastDockedWindowOrMainWindow:
		SearchPreference = MakeShared<FTabManager::FLastMajorOrNomadTab>(TEXT("LevelEditor"));
		break;
	case EAssetEditorOpenLocation::LastDockedWindowOrContentBrowser:
		SearchPreference = MakeShared<FTabManager::FLastMajorOrNomadTab>(TEXT("ContentBrowserTab1"));
		break;
	default:
		SearchPreference = MakeShared<FTabManager::FLiveTabSearch>();
		break;
	}

	FGlobalTabmanager::Get()->InsertNewDocumentTab(PlaceholderId, *SearchPreference, Tab);

	if (TSharedPtr<SWindow> Window = Tab->GetParentWindow())
	{
		Window->BringToFront();
	}

	OpenTabs.Add(Key, Tab);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FTextFileBrowserModule, TextFileBrowser)
