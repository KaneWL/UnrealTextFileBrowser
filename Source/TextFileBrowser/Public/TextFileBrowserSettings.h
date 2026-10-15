// Copyright (C) 2026 Kane. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "TextFileBrowserSettings.generated.h"

/** 额外挂载：把任意磁盘目录映射到 Content Browser 的一个根路径下 */
USTRUCT()
struct FTextFileBrowserMount
{
	GENERATED_BODY()

	/** Content Browser 中显示的路径，例如 /Doc */
	UPROPERTY(EditAnywhere, Config, Category = "Mount")
	FString MountPath;

	/** 磁盘目录。相对路径以项目目录(FirisGame.uproject 所在目录)为基准 */
	UPROPERTY(EditAnywhere, Config, Category = "Mount")
	FString DiskPath;
};

/** 语法高亮配色 */
USTRUCT()
struct FTextFileHighlightColors
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Config, Category = "Colors")
	FLinearColor Background = FLinearColor(0.012f, 0.012f, 0.012f, 1.0f);

	UPROPERTY(EditAnywhere, Config, Category = "Colors")
	FLinearColor Normal = FLinearColor(0.85f, 0.85f, 0.85f, 1.0f);

	UPROPERTY(EditAnywhere, Config, Category = "Colors")
	FLinearColor Keyword = FLinearColor(0.26f, 0.56f, 0.93f, 1.0f);

	/** 次级关键字 / 内建类型 */
	UPROPERTY(EditAnywhere, Config, Category = "Colors")
	FLinearColor Type = FLinearColor(0.30f, 0.80f, 0.75f, 1.0f);

	UPROPERTY(EditAnywhere, Config, Category = "Colors")
	FLinearColor String = FLinearColor(0.85f, 0.53f, 0.38f, 1.0f);

	UPROPERTY(EditAnywhere, Config, Category = "Colors")
	FLinearColor Number = FLinearColor(0.71f, 0.85f, 0.60f, 1.0f);

	UPROPERTY(EditAnywhere, Config, Category = "Colors")
	FLinearColor Comment = FLinearColor(0.42f, 0.60f, 0.33f, 1.0f);

	UPROPERTY(EditAnywhere, Config, Category = "Colors")
	FLinearColor Preprocessor = FLinearColor(0.77f, 0.53f, 0.85f, 1.0f);

	UPROPERTY(EditAnywhere, Config, Category = "Colors")
	FLinearColor Operator = FLinearColor(0.85f, 0.85f, 0.60f, 1.0f);

	/** 代码补全列表中已选中项的高亮色 */
	UPROPERTY(EditAnywhere, Config, Category = "Colors")
	FLinearColor CompletionSelection = FLinearColor(0.02f, 0.26f, 0.55f, 1.0f);
};

/** 一种语言的高亮 / 缩进规则 */
USTRUCT()
struct FTextFileLanguage
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Config, Category = "Language")
	FString Name;

	/** 使用该规则的扩展名（不含点） */
	UPROPERTY(EditAnywhere, Config, Category = "Language")
	TArray<FString> Extensions;

	UPROPERTY(EditAnywhere, Config, Category = "Language")
	TArray<FString> Keywords;

	/** 次级关键字 / 内建类型，使用 Type 颜色 */
	UPROPERTY(EditAnywhere, Config, Category = "Language")
	TArray<FString> Types;

	/** 单行注释前缀，如 // # -- ; */
	UPROPERTY(EditAnywhere, Config, Category = "Language")
	TArray<FString> LineCommentPrefixes;

	UPROPERTY(EditAnywhere, Config, Category = "Language")
	FString BlockCommentStart;

	UPROPERTY(EditAnywhere, Config, Category = "Language")
	FString BlockCommentEnd;

	/** 字符串定界符，每个字符一种 */
	UPROPERTY(EditAnywhere, Config, Category = "Language")
	FString StringDelimiters = TEXT("\"'");

	/** 行首 # 开头的指令按预处理颜色显示 */
	UPROPERTY(EditAnywhere, Config, Category = "Language")
	bool bPreprocessorDirectives = false;

	UPROPERTY(EditAnywhere, Config, Category = "Language")
	bool bCaseSensitive = true;

	/** 一行以这些片段结尾时，回车后下一行自动多缩进一级 */
	UPROPERTY(EditAnywhere, Config, Category = "Language")
	TArray<FString> IndentTriggers;
};

/** 配置独立保存在 Config/DefaultTextFileBrowser.ini，不写入 DefaultEditor.ini */
UCLASS(Config = TextFileBrowser, DefaultConfig, meta = (DisplayName = "Text File Browser"))
class TEXTFILEBROWSER_API UTextFileBrowserSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UTextFileBrowserSettings();

	virtual FName GetContainerName() const override { return TEXT("Editor"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	// ---------- 文件 ----------

	/** 会被识别为文本并显示在 Content Browser 中的扩展名（不含点，大小写不敏感） */
	UPROPERTY(EditAnywhere, Config, Category = "Files")
	TArray<FString> TextExtensions;

	/** 允许通过 Content Browser 右键 "Add New" 直接新建的扩展名 */
	UPROPERTY(EditAnywhere, Config, Category = "Files")
	TArray<FString> CreatableExtensions;

	/** 超过该大小的文件不再用内置编辑器打开，而是改用外部程序 */
	UPROPERTY(EditAnywhere, Config, Category = "Files", meta = (ClampMin = 1, UIMin = 64, UIMax = 65536))
	int32 MaxEditableFileSizeKB = 2048;

	// ---------- 挂载 ----------

	/** 挂载项目 Content 目录 (/Game) */
	UPROPERTY(EditAnywhere, Config, Category = "Mounts")
	bool bMountProjectContent = true;

	/** 挂载所有项目插件的 Content 目录 */
	UPROPERTY(EditAnywhere, Config, Category = "Mounts")
	bool bMountProjectPlugins = true;

	/** 挂载引擎 Content 目录 (/Engine) */
	UPROPERTY(EditAnywhere, Config, Category = "Mounts")
	bool bMountEngineContent = false;

	/** 把项目 Source 目录挂载为 /ProjectSource，方便直接查看 C++ / Build.cs */
	UPROPERTY(EditAnywhere, Config, Category = "Mounts")
	bool bMountProjectSource = true;

	/** 额外挂载的目录 */
	UPROPERTY(EditAnywhere, Config, Category = "Mounts")
	TArray<FTextFileBrowserMount> ExtraMounts;

	// ---------- 编辑器 ----------

	UPROPERTY(EditAnywhere, Config, Category = "Editor", meta = (ClampMin = 6, ClampMax = 48))
	int32 FontSize = 10;

	UPROPERTY(EditAnywhere, Config, Category = "Editor")
	bool bWordWrap = false;

	/** Tab 显示宽度（空格数） */
	UPROPERTY(EditAnywhere, Config, Category = "Editor", meta = (ClampMin = 1, ClampMax = 16))
	int32 TabSize = 4;

	/** 按 Tab 时插入空格而不是制表符 */
	UPROPERTY(EditAnywhere, Config, Category = "Editor")
	bool bInsertSpacesForTab = false;

	/** 回车时沿用上一行缩进，并按语言规则自动增加缩进 */
	UPROPERTY(EditAnywhere, Config, Category = "Editor")
	bool bAutoIndent = true;

	/** 超过该大小的文件关闭语法高亮以保证流畅 */
	UPROPERTY(EditAnywhere, Config, Category = "Editor", meta = (ClampMin = 1, UIMin = 64, UIMax = 65536))
	int32 MaxHighlightFileSizeKB = 512;

	// ---------- 代码补全 ----------

	/** 输入时自动弹出补全（关键字 + 当前文档中出现过的单词），也可随时 Ctrl+Space 触发 */
	UPROPERTY(EditAnywhere, Config, Category = "Completion")
	bool bEnableCompletion = true;

	/** 自动弹出补全所需的最少已输入字符数 */
	UPROPERTY(EditAnywhere, Config, Category = "Completion", meta = (ClampMin = 1, ClampMax = 10))
	int32 CompletionMinPrefixLength = 2;

	/** 补全列表最多显示条数 */
	UPROPERTY(EditAnywhere, Config, Category = "Completion", meta = (ClampMin = 3, ClampMax = 100))
	int32 CompletionMaxItems = 30;

	// ---------- 高亮 ----------

	/** 高亮配色。也可直接编辑 Config/DefaultTextFileBrowser.ini 中 [/Script/TextFileBrowser.TextFileBrowserSettings] 下的 HighlightColors */
	UPROPERTY(EditAnywhere, Config, Category = "Highlight")
	FTextFileHighlightColors HighlightColors;

	/** 各语言高亮规则，按扩展名匹配 */
	UPROPERTY(EditAnywhere, Config, Category = "Highlight")
	TArray<FTextFileLanguage> Languages;

	/** 根据文件名找语言规则；找不到返回 nullptr */
	const FTextFileLanguage* FindLanguageForFile(const FString& InFilename) const;

	FString GetIndentUnit() const;

	DECLARE_MULTICAST_DELEGATE(FOnSettingsChanged);
	FOnSettingsChanged OnSettingsChanged;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
