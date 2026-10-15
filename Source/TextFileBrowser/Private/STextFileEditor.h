// Copyright (C) 2026 Kane. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Framework/Text/TextLayout.h"

class SDockTab;
class SMultiLineEditableText;
class SMultiLineEditableTextBox;
class STextFileEditableTextBox;
class IMenu;
class IInputProcessor;
class ITableRow;
class STableViewBase;
template <typename ItemType> class SListView;
class FTextFileSyntaxHighlighter;
struct FTextFileLanguage;

/**
 * 内置的简易文本编辑器：
 *  - 加载 / 编辑 / 保存单个文本文件，保留原文件的编码与换行风格
 *  - 按扩展名做语法高亮（配色见 Editor Preferences > Plugins > Text File Browser）
 *  - 回车自动缩进、Tab 插入、Ctrl+S 保存、Ctrl+滚轮 / Ctrl+= / Ctrl+- 缩放
 *  - 简易代码补全：关键字 + 文档中已出现的单词（自动弹出 / Ctrl+Space）
 */
class STextFileEditor : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STextFileEditor) {}
		SLATE_ARGUMENT(FString, Filename)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~STextFileEditor() override;

	void SetOwnerTab(const TSharedRef<SDockTab>& InTab) { OwnerTab = InTab; }

	bool IsLoaded() const { return bLoaded; }
	bool IsDirty() const { return bDirty; }
	const FString& GetFilename() const { return Filename; }

	FText GetTabLabel() const;

	bool Save();
	bool Reload();

	/** 标签页关闭前调用；返回 false 表示用户取消关闭 */
	bool HandleCloseRequest();

	/** 由输入预处理器调用：鼠标在文本框上且按住 Ctrl 滚动时缩放字号，返回 true 表示已消费 */
	bool HandleCtrlMouseWheel(const FPointerEvent& InWheelEvent);

	// SWidget
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	enum class EFileEncoding : uint8
	{
		Utf8,
		Utf8Bom,
		Utf16LE,
		Utf16BE,
	};

	// ---------- 构建 ----------
	TSharedRef<SWidget> BuildToolbar();
	TSharedRef<SWidget> BuildStatusBar();
	void ApplySettings();

	// ---------- 文件 ----------
	bool LoadFromDisk(FString& OutText);
	void ApplyLoadedText(const FString& InText);
	void CheckExternalModification();

	// ---------- 编辑 ----------
	FReply HandleTextBoxKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent);
	void HandleTextChanged(const FText& InText);
	void HandleCursorMoved(const FTextLocation& InLocation);
	bool HandleEnterKey();
	bool HandleTabKey(bool bShift);

	/** 取当前行文本以及光标所在行的起始偏移 */
	FString GetLineText(int32 LineIndex) const;
	FString GetWordBeforeCursor() const;

	// ---------- 补全 ----------
	void RebuildWordIndex();
	void TriggerCompletion(bool bManual);
	void UpdateCompletionCandidates(const FString& Prefix);
	void ShowCompletionPopup();
	void DismissCompletion();
	void MoveCompletionSelection(int32 Delta);
	void AcceptCompletion();
	bool IsCompletionOpen() const;
	FVector2D ComputeCursorScreenPosition() const;
	TSharedRef<ITableRow> GenerateCompletionRow(TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void HandleCompletionRowClicked(TSharedPtr<FString> Item);

	// ---------- 视图 ----------
	FSlateFontInfo GetEditorFont() const;
	bool IsWordWrapEnabled() const { return bWordWrap; }
	void ToggleWordWrap();
	void ToggleHighlight();
	void AdjustFontSize(int32 Delta);
	void SaveViewPrefs() const;

	FText GetStatusText() const;
	FText GetLanguageText() const;

	void OpenInExternalEditor() const;
	void ShowInExplorer() const;

	static void Notify(const FText& InMessage, bool bSuccess);

	// ---------- 状态 ----------
	FString Filename;
	TSharedPtr<STextFileEditableTextBox> TextBox;
	TSharedPtr<FTextFileSyntaxHighlighter> Highlighter;
	TSharedPtr<IInputProcessor> WheelZoomProcessor;
	TWeakPtr<SDockTab> OwnerTab;

	FString CachedText;
	int32 PreviousTextLength = 0;

	bool bLoaded = false;
	bool bDirty = false;
	bool bUseCRLF = true;
	EFileEncoding Encoding = EFileEncoding::Utf8;
	FDateTime LoadedTimestamp;
	int64 LoadedFileSize = 0;
	double LastExternalCheckTime = 0.0;
	bool bWarnedExternalChange = false;

	int32 FontSize = 10;
	bool bWordWrap = false;
	bool bHighlightEnabled = true;
	bool bHighlightForcedOff = false;
	FTextLocation CursorLocation;

	// 补全
	TSet<FString> WordIndex;
	bool bWordIndexDirty = true;
	double LastWordIndexTime = 0.0;
	TArray<TSharedPtr<FString>> CompletionItems;
	TSharedPtr<SListView<TSharedPtr<FString>>> CompletionList;
	TSharedPtr<IMenu> CompletionMenu;
	int32 CompletionSelectedIndex = 0;
	FString CompletionPrefix;
	bool bSuppressCompletion = false;
};
