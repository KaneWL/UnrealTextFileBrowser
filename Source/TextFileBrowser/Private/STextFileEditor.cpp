// Copyright (C) 2026 Kane. All Rights Reserved.

#include "STextFileEditor.h"

#include "TextFileBrowserSettings.h"
#include "TextFileSyntaxHighlighter.h"

#include "Fonts/FontMeasure.h"
#include "Brushes/SlateColorBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "TextFileBrowser"

namespace
{
	const TCHAR* ViewPrefsSection = TEXT("TextFileBrowser.View");
	constexpr int32 MaxWordIndexSizeKB = 1024;
	constexpr double WordIndexRebuildInterval = 0.6;
	constexpr double ExternalCheckInterval = 2.0;
	constexpr int32 CompletionVisibleRows = 10;

	using FScopedEditableTextTransaction = SMultiLineEditableText::FScopedEditableTextTransaction;
}

/** 暴露内部 SMultiLineEditableText 以便做选区 / 插入等操作 */
class STextFileEditableTextBox : public SMultiLineEditableTextBox
{
public:
	TSharedPtr<SMultiLineEditableText> GetEditableText() const { return EditableText; }
};

/** 文本框会先把滚轮当成滚动吃掉，所以 Ctrl+滚轮要在输入预处理阶段拦截 */
class FTextFileEditorWheelZoomProcessor : public IInputProcessor
{
public:
	explicit FTextFileEditorWheelZoomProcessor(const TSharedRef<STextFileEditor>& InEditor) : Editor(InEditor) {}

	virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}

	virtual bool HandleMouseWheelOrGestureEvent(FSlateApplication&, const FPointerEvent& InWheelEvent, const FPointerEvent*) override
	{
		if (TSharedPtr<STextFileEditor> Pinned = Editor.Pin())
		{
			return Pinned->HandleCtrlMouseWheel(InWheelEvent);
		}
		return false;
	}

	virtual const TCHAR* GetDebugName() const override { return TEXT("TextFileEditorWheelZoom"); }

private:
	TWeakPtr<STextFileEditor> Editor;
};

// ====================================================================================================
// 构建
// ====================================================================================================

void STextFileEditor::Construct(const FArguments& InArgs)
{
	Filename = FPaths::ConvertRelativePathToFull(InArgs._Filename);
	SetCanTick(true);

	const UTextFileBrowserSettings* Settings = GetDefault<UTextFileBrowserSettings>();
	FontSize = Settings->FontSize;
	bWordWrap = Settings->bWordWrap;
	if (GConfig)
	{
		GConfig->GetInt(ViewPrefsSection, TEXT("FontSize"), FontSize, GEditorPerProjectIni);
		GConfig->GetBool(ViewPrefsSection, TEXT("WordWrap"), bWordWrap, GEditorPerProjectIni);
		GConfig->GetBool(ViewPrefsSection, TEXT("Highlight"), bHighlightEnabled, GEditorPerProjectIni);
	}
	FontSize = FMath::Clamp(FontSize, 6, 48);

	Highlighter = FTextFileSyntaxHighlighter::Create(Settings->FindLanguageForFile(Filename), Settings->HighlightColors, GetEditorFont(), Settings->TabSize);

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			BuildToolbar()
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SAssignNew(TextBox, STextFileEditableTextBox)
			.Marshaller(Highlighter)
			.Font(this, &STextFileEditor::GetEditorFont)
			.AutoWrapText(this, &STextFileEditor::IsWordWrapEnabled)
			.BackgroundColor(FSlateColor(Settings->HighlightColors.Background))
			.ForegroundColor(FSlateColor(Settings->HighlightColors.Normal))
			.AlwaysShowScrollbars(true)
			.AllowContextMenu(true)
			// 引擎默认按 ICU 词边界选词，会把 a.b.c 整串选中；改由 OnMouseButtonDoubleClick 按标识符选词
			.SelectWordOnMouseDoubleClick(false)
			.ModiferKeyForNewLine(EModifierKey::None)
			.OnIsTypedCharValid_Lambda([](const TCHAR) { return true; })
			.OnTextChanged(this, &STextFileEditor::HandleTextChanged)
			.OnCursorMoved(this, &STextFileEditor::HandleCursorMoved)
			.OnKeyDownHandler(this, &STextFileEditor::HandleTextBoxKeyDown)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			BuildStatusBar()
		]
	];

	FString Text;
	if (LoadFromDisk(Text))
	{
		ApplyLoadedText(Text);
	}
	else
	{
		Notify(FText::Format(LOCTEXT("LoadFailed", "无法读取文件：{0}"), FText::FromString(Filename)), false);
	}

	WheelZoomProcessor = MakeShared<FTextFileEditorWheelZoomProcessor>(SharedThis(this));
	FSlateApplication::Get().RegisterInputPreProcessor(WheelZoomProcessor);
}

STextFileEditor::~STextFileEditor()
{
	if (WheelZoomProcessor.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(WheelZoomProcessor);
		WheelZoomProcessor.Reset();
	}
	if (CompletionMenu.IsValid())
	{
		CompletionMenu->Dismiss();
		CompletionMenu.Reset();
	}
}

TSharedRef<SWidget> STextFileEditor::BuildToolbar()
{
	FSlimHorizontalToolBarBuilder Toolbar(nullptr, FMultiBoxCustomization::None);

	Toolbar.AddToolBarButton(
		FUIAction(FExecuteAction::CreateLambda([this]() { Save(); }), FCanExecuteAction::CreateLambda([this]() { return bLoaded; })),
		NAME_None,
		LOCTEXT("Save", "保存"),
		LOCTEXT("SaveTooltip", "保存到磁盘 (Ctrl+S)"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Save"));

	Toolbar.AddToolBarButton(
		FUIAction(FExecuteAction::CreateLambda([this]() { Reload(); })),
		NAME_None,
		LOCTEXT("Reload", "重新载入"),
		LOCTEXT("ReloadTooltip", "放弃当前修改，从磁盘重新读取"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Refresh"));

	Toolbar.AddSeparator();

	Toolbar.AddToolBarButton(
		FUIAction(FExecuteAction::CreateLambda([this]() { OpenInExternalEditor(); })),
		NAME_None,
		LOCTEXT("OpenExternal", "外部打开"),
		LOCTEXT("OpenExternalTooltip", "用系统默认程序打开此文件"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.OpenInExternalEditor"));

	Toolbar.AddToolBarButton(
		FUIAction(FExecuteAction::CreateLambda([this]() { ShowInExplorer(); })),
		NAME_None,
		LOCTEXT("ShowInExplorer", "所在目录"),
		LOCTEXT("ShowInExplorerTooltip", "在资源管理器中显示"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.FolderOpen"));

	Toolbar.AddSeparator();

	Toolbar.AddToolBarButton(
		FUIAction(
			FExecuteAction::CreateLambda([this]() { ToggleWordWrap(); }),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([this]() { return bWordWrap; })),
		NAME_None,
		LOCTEXT("WordWrap", "自动换行"),
		LOCTEXT("WordWrapTooltip", "切换自动换行 (Alt+Z)"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Edit"),
		EUserInterfaceActionType::ToggleButton);

	Toolbar.AddToolBarButton(
		FUIAction(
			FExecuteAction::CreateLambda([this]() { ToggleHighlight(); }),
			FCanExecuteAction::CreateLambda([this]() { return !bHighlightForcedOff && Highlighter.IsValid() && Highlighter->GetLanguage() != nullptr; }),
			FIsActionChecked::CreateLambda([this]() { return bHighlightEnabled && !bHighlightForcedOff; })),
		NAME_None,
		LOCTEXT("Highlight", "语法高亮"),
		LOCTEXT("HighlightTooltip", "切换语法高亮"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Documentation"),
		EUserInterfaceActionType::ToggleButton);

	Toolbar.AddSeparator();

	Toolbar.AddToolBarButton(
		FUIAction(FExecuteAction::CreateLambda([this]() { AdjustFontSize(-1); })),
		NAME_None,
		LOCTEXT("FontSmaller", "缩小"),
		LOCTEXT("FontSmallerTooltip", "减小字号 (Ctrl+- / Ctrl+滚轮向下)"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Minus"));

	Toolbar.AddToolBarButton(
		FUIAction(FExecuteAction::CreateLambda([this]() { AdjustFontSize(+1); })),
		NAME_None,
		LOCTEXT("FontLarger", "放大"),
		LOCTEXT("FontLargerTooltip", "增大字号 (Ctrl+= / Ctrl+滚轮向上)"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Plus"));

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
		.Padding(FMargin(4.0f, 2.0f))
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				Toolbar.MakeWidget()
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(8.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Filename))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.OverflowPolicy(ETextOverflowPolicy::MiddleEllipsis)
				.ToolTipText(FText::FromString(Filename))
			]
		];
}

TSharedRef<SWidget> STextFileEditor::BuildStatusBar()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
		.Padding(FMargin(8.0f, 2.0f))
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &STextFileEditor::GetStatusText)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &STextFileEditor::GetLanguageText)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
		];
}

void STextFileEditor::ApplySettings()
{
	const UTextFileBrowserSettings* Settings = GetDefault<UTextFileBrowserSettings>();
	if (Highlighter.IsValid())
	{
		Highlighter->UpdateStyle(Settings->HighlightColors, GetEditorFont(), Settings->TabSize);
		Highlighter->SetHighlightEnabled(bHighlightEnabled && !bHighlightForcedOff);
	}
	if (TextBox.IsValid())
	{
		TextBox->SetTextBoxBackgroundColor(FSlateColor(Settings->HighlightColors.Background));
		TextBox->SetTextBoxForegroundColor(FSlateColor(Settings->HighlightColors.Normal));
	}
}

// ====================================================================================================
// 文件读写
// ====================================================================================================

bool STextFileEditor::LoadFromDisk(FString& OutText)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Filename))
	{
		return false;
	}

	Encoding = EFileEncoding::Utf8;
	if (Bytes.Num() >= 3 && Bytes[0] == 0xEF && Bytes[1] == 0xBB && Bytes[2] == 0xBF)
	{
		Encoding = EFileEncoding::Utf8Bom;
	}
	else if (Bytes.Num() >= 2 && Bytes[0] == 0xFF && Bytes[1] == 0xFE)
	{
		Encoding = EFileEncoding::Utf16LE;
	}
	else if (Bytes.Num() >= 2 && Bytes[0] == 0xFE && Bytes[1] == 0xFF)
	{
		Encoding = EFileEncoding::Utf16BE;
	}

	FFileHelper::BufferToString(OutText, Bytes.GetData(), Bytes.Num());

	// 换行风格：有 \r\n 则保留 CRLF，否则有 \n 用 LF，空文件默认 CRLF
	if (OutText.Contains(TEXT("\r\n")))
	{
		bUseCRLF = true;
	}
	else if (OutText.Contains(TEXT("\n")))
	{
		bUseCRLF = false;
	}
	else
	{
		bUseCRLF = true;
	}

	LoadedTimestamp = IFileManager::Get().GetTimeStamp(*Filename);
	LoadedFileSize = Bytes.Num();
	return true;
}

void STextFileEditor::ApplyLoadedText(const FString& InText)
{
	const UTextFileBrowserSettings* Settings = GetDefault<UTextFileBrowserSettings>();

	// 超大文件关闭高亮
	bHighlightForcedOff = LoadedFileSize > (int64)Settings->MaxHighlightFileSizeKB * 1024;
	if (Highlighter.IsValid())
	{
		Highlighter->SetHighlightEnabled(bHighlightEnabled && !bHighlightForcedOff);
	}

	DismissCompletion();

	bSuppressCompletion = true;
	TextBox->SetText(FText::FromString(InText));
	bSuppressCompletion = false;

	CachedText = InText;
	PreviousTextLength = InText.Len();
	bLoaded = true;
	bDirty = false;
	bWarnedExternalChange = false;
	bWordIndexDirty = true;
	LastWordIndexTime = 0.0;
}

bool STextFileEditor::Save()
{
	if (!bLoaded)
	{
		return false;
	}

	// 统一换行，再按原始风格写回
	FString Text = TextBox->GetText().ToString();
	Text.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
	Text.ReplaceInline(TEXT("\r"), TEXT("\n"));
	if (bUseCRLF)
	{
		Text.ReplaceInline(TEXT("\n"), TEXT("\r\n"));
	}

	if (IFileManager::Get().IsReadOnly(*Filename))
	{
		const EAppReturnType::Type Answer = FMessageDialog::Open(EAppMsgType::YesNo,
			FText::Format(LOCTEXT("ReadOnlyPrompt", "文件是只读的：\n{0}\n\n是否清除只读属性并保存？"), FText::FromString(Filename)));
		if (Answer != EAppReturnType::Yes)
		{
			return false;
		}
		FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*Filename, false);
	}

	FFileHelper::EEncodingOptions EncodingOption = FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM;
	switch (Encoding)
	{
	case EFileEncoding::Utf8Bom: EncodingOption = FFileHelper::EEncodingOptions::ForceUTF8; break;
	case EFileEncoding::Utf16LE:
	case EFileEncoding::Utf16BE: EncodingOption = FFileHelper::EEncodingOptions::ForceUnicode; break;
	default: break;
	}

	if (!FFileHelper::SaveStringToFile(Text, *Filename, EncodingOption))
	{
		Notify(FText::Format(LOCTEXT("SaveFailed", "保存失败：{0}"), FText::FromString(Filename)), false);
		return false;
	}

	LoadedTimestamp = IFileManager::Get().GetTimeStamp(*Filename);
	LoadedFileSize = IFileManager::Get().FileSize(*Filename);
	bDirty = false;
	bWarnedExternalChange = false;
	Notify(FText::Format(LOCTEXT("Saved", "已保存 {0}"), FText::FromString(FPaths::GetCleanFilename(Filename))), true);
	return true;
}

bool STextFileEditor::Reload()
{
	if (bDirty)
	{
		const EAppReturnType::Type Answer = FMessageDialog::Open(EAppMsgType::YesNo,
			LOCTEXT("ReloadPrompt", "当前有未保存的修改，重新载入会丢失这些修改。确定继续？"));
		if (Answer != EAppReturnType::Yes)
		{
			return false;
		}
	}

	FString Text;
	if (!LoadFromDisk(Text))
	{
		Notify(FText::Format(LOCTEXT("LoadFailed", "无法读取文件：{0}"), FText::FromString(Filename)), false);
		return false;
	}

	const FTextLocation OldCursor = TextBox->GetCursorLocation();
	ApplyLoadedText(Text);
	if (OldCursor.IsValid())
	{
		TextBox->GoTo(OldCursor);
		TextBox->ScrollTo(OldCursor);
	}
	return true;
}

bool STextFileEditor::HandleCloseRequest()
{
	DismissCompletion();

	if (!bDirty)
	{
		return true;
	}

	const EAppReturnType::Type Answer = FMessageDialog::Open(EAppMsgType::YesNoCancel,
		FText::Format(LOCTEXT("ClosePrompt", "{0} 有未保存的修改。\n\n是否保存？"), FText::FromString(FPaths::GetCleanFilename(Filename))));

	switch (Answer)
	{
	case EAppReturnType::Yes:
		return Save();
	case EAppReturnType::No:
		return true;
	default:
		return false;
	}
}

void STextFileEditor::CheckExternalModification()
{
	if (!bLoaded || !FPaths::FileExists(Filename))
	{
		return;
	}

	const FDateTime DiskTimestamp = IFileManager::Get().GetTimeStamp(*Filename);
	if (DiskTimestamp == LoadedTimestamp)
	{
		return;
	}

	if (!bDirty)
	{
		FString Text;
		if (LoadFromDisk(Text))
		{
			const FTextLocation OldCursor = TextBox->GetCursorLocation();
			ApplyLoadedText(Text);
			if (OldCursor.IsValid())
			{
				TextBox->GoTo(OldCursor);
				TextBox->ScrollTo(OldCursor);
			}
			Notify(FText::Format(LOCTEXT("ExternalReloaded", "{0} 已被外部修改，已自动重新载入"), FText::FromString(FPaths::GetCleanFilename(Filename))), true);
		}
	}
	else if (!bWarnedExternalChange)
	{
		bWarnedExternalChange = true;
		Notify(FText::Format(LOCTEXT("ExternalChangedDirty", "{0} 已被外部修改，当前编辑器内有未保存内容，保存会覆盖外部修改"), FText::FromString(FPaths::GetCleanFilename(Filename))), false);
	}
}

// ====================================================================================================
// 编辑
// ====================================================================================================

void STextFileEditor::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (bWordIndexDirty && InCurrentTime - LastWordIndexTime > WordIndexRebuildInterval)
	{
		RebuildWordIndex();
		LastWordIndexTime = InCurrentTime;
	}

	if (InCurrentTime - LastExternalCheckTime > ExternalCheckInterval)
	{
		LastExternalCheckTime = InCurrentTime;
		CheckExternalModification();
	}
}

FReply STextFileEditor::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.IsControlDown() && InKeyEvent.GetKey() == EKeys::S)
	{
		Save();
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

FReply STextFileEditor::OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent)
{
	// 第一次点击已把光标放到了点击处，这里只需以光标为中心向两侧扩展到标识符边界
	if (InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !TextBox.IsValid())
	{
		return FReply::Unhandled();
	}

	TSharedPtr<SMultiLineEditableText> Inner = TextBox->GetEditableText();
	if (!Inner.IsValid())
	{
		return FReply::Unhandled();
	}

	const FTextLocation Cursor = Inner->GetCursorLocation();
	if (!Cursor.IsValid())
	{
		return FReply::Unhandled();
	}

	const FString Line = GetLineText(Cursor.GetLineIndex());
	const int32 Offset = FMath::Clamp(Cursor.GetOffset(), 0, Line.Len());

	auto IsWordChar = [](TCHAR C) { return FTextFileSyntaxHighlighter::IsIdentifierChar(C); };

	int32 Start = Offset;
	int32 End = Offset;
	if ((Offset < Line.Len() && IsWordChar(Line[Offset])) || (Offset > 0 && IsWordChar(Line[Offset - 1])))
	{
		while (Start > 0 && IsWordChar(Line[Start - 1])) { --Start; }
		while (End < Line.Len() && IsWordChar(Line[End])) { ++End; }
	}
	else if (Offset < Line.Len())
	{
		// 点在符号上：只选中该符号
		End = Offset + 1;
	}

	if (End > Start)
	{
		Inner->SelectText(FTextLocation(Cursor.GetLineIndex(), Start), FTextLocation(Cursor.GetLineIndex(), End));
	}
	return FReply::Handled();
}

bool STextFileEditor::HandleCtrlMouseWheel(const FPointerEvent& InWheelEvent)
{
	if (!(InWheelEvent.IsControlDown() || InWheelEvent.IsCommandDown()) || !TextBox.IsValid())
	{
		return false;
	}
	// 只在鼠标落在本编辑器的文本框范围内时生效，避免影响其它窗口
	if (!TextBox->GetCachedGeometry().IsUnderLocation(InWheelEvent.GetScreenSpacePosition()))
	{
		return false;
	}

	const float Delta = InWheelEvent.GetWheelDelta();
	if (FMath::IsNearlyZero(Delta))
	{
		return true;
	}
	AdjustFontSize(Delta > 0.0f ? +1 : -1);
	return true;
}

FReply STextFileEditor::HandleTextBoxKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	const bool bCtrl = InKeyEvent.IsControlDown() || InKeyEvent.IsCommandDown();

	if (bCtrl && Key == EKeys::S)
	{
		Save();
		return FReply::Handled();
	}
	if (bCtrl && (Key == EKeys::Equals || Key == EKeys::Add))
	{
		AdjustFontSize(+1);
		return FReply::Handled();
	}
	if (bCtrl && (Key == EKeys::Hyphen || Key == EKeys::Subtract))
	{
		AdjustFontSize(-1);
		return FReply::Handled();
	}
	if (InKeyEvent.IsAltDown() && Key == EKeys::Z)
	{
		ToggleWordWrap();
		return FReply::Handled();
	}
	if (bCtrl && Key == EKeys::SpaceBar)
	{
		TriggerCompletion(true);
		return FReply::Handled();
	}

	if (IsCompletionOpen())
	{
		if (Key == EKeys::Escape)
		{
			DismissCompletion();
			return FReply::Handled();
		}
		if (Key == EKeys::Up)
		{
			MoveCompletionSelection(-1);
			return FReply::Handled();
		}
		if (Key == EKeys::Down)
		{
			MoveCompletionSelection(+1);
			return FReply::Handled();
		}
		if (Key == EKeys::PageUp)
		{
			MoveCompletionSelection(-CompletionVisibleRows);
			return FReply::Handled();
		}
		if (Key == EKeys::PageDown)
		{
			MoveCompletionSelection(+CompletionVisibleRows);
			return FReply::Handled();
		}
		if (Key == EKeys::Enter || Key == EKeys::Tab)
		{
			AcceptCompletion();
			return FReply::Handled();
		}
		if (Key == EKeys::Left || Key == EKeys::Right || Key == EKeys::Home || Key == EKeys::End)
		{
			DismissCompletion();
			return FReply::Unhandled();
		}
	}

	if (Key == EKeys::Enter && !bCtrl)
	{
		return HandleEnterKey() ? FReply::Handled() : FReply::Unhandled();
	}
	if (Key == EKeys::Tab && !bCtrl)
	{
		return HandleTabKey(InKeyEvent.IsShiftDown()) ? FReply::Handled() : FReply::Unhandled();
	}

	return FReply::Unhandled();
}

void STextFileEditor::HandleTextChanged(const FText& InText)
{
	CachedText = InText.ToString();
	const int32 NewLength = CachedText.Len();
	const bool bTypedOneChar = NewLength == PreviousTextLength + 1;
	PreviousTextLength = NewLength;

	if (!bLoaded)
	{
		return;
	}

	bDirty = true;
	bWordIndexDirty = true;

	if (bSuppressCompletion)
	{
		return;
	}

	const UTextFileBrowserSettings* Settings = GetDefault<UTextFileBrowserSettings>();
	if (!Settings->bEnableCompletion)
	{
		DismissCompletion();
		return;
	}

	const FString Prefix = GetWordBeforeCursor();
	if (IsCompletionOpen())
	{
		if (Prefix.IsEmpty())
		{
			DismissCompletion();
		}
		else
		{
			UpdateCompletionCandidates(Prefix);
			if (CompletionItems.Num() == 0)
			{
				DismissCompletion();
			}
			else if (CompletionList.IsValid())
			{
				CompletionList->RequestListRefresh();
				CompletionSelectedIndex = 0;
				CompletionList->SetSelection(CompletionItems[0]);
			}
		}
	}
	else if (bTypedOneChar && Prefix.Len() >= Settings->CompletionMinPrefixLength)
	{
		TriggerCompletion(false);
	}
}

void STextFileEditor::HandleCursorMoved(const FTextLocation& InLocation)
{
	// 光标跳到别的行时关闭补全
	if (IsCompletionOpen() && CursorLocation.IsValid() && InLocation.GetLineIndex() != CursorLocation.GetLineIndex())
	{
		DismissCompletion();
	}
	CursorLocation = InLocation;
}

FString STextFileEditor::GetLineText(int32 LineIndex) const
{
	FString Line;
	if (TextBox.IsValid() && TextBox->GetEditableText().IsValid() && LineIndex >= 0)
	{
		TextBox->GetEditableText()->GetTextLine(LineIndex, Line);
	}
	return Line;
}

FString STextFileEditor::GetWordBeforeCursor() const
{
	if (!TextBox.IsValid())
	{
		return FString();
	}

	const FTextLocation Cursor = TextBox->GetCursorLocation();
	if (!Cursor.IsValid())
	{
		return FString();
	}

	const FString Line = GetLineText(Cursor.GetLineIndex());
	const int32 Offset = FMath::Clamp(Cursor.GetOffset(), 0, Line.Len());

	int32 Start = Offset;
	while (Start > 0 && FTextFileSyntaxHighlighter::IsIdentifierChar(Line[Start - 1]))
	{
		--Start;
	}
	// 以数字开头的不是标识符
	if (Start < Offset && FChar::IsDigit(Line[Start]))
	{
		return FString();
	}
	return Line.Mid(Start, Offset - Start);
}

bool STextFileEditor::HandleEnterKey()
{
	const UTextFileBrowserSettings* Settings = GetDefault<UTextFileBrowserSettings>();
	if (!Settings->bAutoIndent || !TextBox.IsValid())
	{
		return false;
	}

	TSharedPtr<SMultiLineEditableText> Inner = TextBox->GetEditableText();
	if (!Inner.IsValid())
	{
		return false;
	}

	const FTextLocation Cursor = Inner->GetCursorLocation();
	if (!Cursor.IsValid())
	{
		return false;
	}

	const FString Line = GetLineText(Cursor.GetLineIndex());
	const int32 Offset = FMath::Clamp(Cursor.GetOffset(), 0, Line.Len());

	int32 IndentLen = 0;
	while (IndentLen < Offset && (Line[IndentLen] == TEXT(' ') || Line[IndentLen] == TEXT('\t')))
	{
		++IndentLen;
	}
	const FString BaseIndent = Line.Left(IndentLen);

	// 光标前内容以缩进触发片段结尾 → 多缩进一级
	bool bExtraIndent = false;
	const FString Before = Line.Left(Offset).TrimEnd();
	if (const FTextFileLanguage* Lang = Highlighter.IsValid() ? Highlighter->GetLanguage() : nullptr)
	{
		for (const FString& Trigger : Lang->IndentTriggers)
		{
			if (!Trigger.IsEmpty() && Before.EndsWith(Trigger, Lang->bCaseSensitive ? ESearchCase::CaseSensitive : ESearchCase::IgnoreCase))
			{
				// 以单词触发（then/do/else）时要求是完整单词
				if (FTextFileSyntaxHighlighter::IsIdentifierChar(Trigger[0]))
				{
					const int32 WordStart = Before.Len() - Trigger.Len();
					if (WordStart > 0 && FTextFileSyntaxHighlighter::IsIdentifierChar(Before[WordStart - 1]))
					{
						continue;
					}
				}
				bExtraIndent = true;
				break;
			}
		}
	}

	const FString IndentUnit = Settings->GetIndentUnit();
	FString Insert = TEXT("\n") + BaseIndent + (bExtraIndent ? IndentUnit : FString());

	// 光标紧贴闭合括号且刚增加了缩进：{|} → 把闭合括号放到下一行并保持光标在中间
	bool bPlaceCursorInMiddle = false;
	if (bExtraIndent && Offset < Line.Len())
	{
		const TCHAR Next = Line[Offset];
		if (Next == TEXT('}') || Next == TEXT(']') || Next == TEXT(')'))
		{
			Insert += TEXT("\n") + BaseIndent;
			bPlaceCursorInMiddle = true;
		}
	}

	bSuppressCompletion = true;
	DismissCompletion();
	{
		FScopedEditableTextTransaction Transaction(Inner);
		Inner->InsertTextAtCursor(Insert);
		if (bPlaceCursorInMiddle)
		{
			const FTextLocation Middle(Cursor.GetLineIndex() + 1, BaseIndent.Len() + IndentUnit.Len());
			Inner->GoTo(Middle);
		}
	}
	Inner->ScrollTo(Inner->GetCursorLocation());
	bSuppressCompletion = false;
	return true;
}

bool STextFileEditor::HandleTabKey(bool bShift)
{
	if (!TextBox.IsValid())
	{
		return false;
	}

	TSharedPtr<SMultiLineEditableText> Inner = TextBox->GetEditableText();
	if (!Inner.IsValid())
	{
		return false;
	}

	const UTextFileBrowserSettings* Settings = GetDefault<UTextFileBrowserSettings>();
	const FString IndentUnit = Settings->GetIndentUnit();
	const int32 TabSize = FMath::Max(1, Settings->TabSize);

	auto Unindent = [TabSize](const FString& InLine) -> FString
	{
		if (InLine.StartsWith(TEXT("\t")))
		{
			return InLine.RightChop(1);
		}
		int32 Spaces = 0;
		while (Spaces < InLine.Len() && Spaces < TabSize && InLine[Spaces] == TEXT(' '))
		{
			++Spaces;
		}
		return InLine.RightChop(Spaces);
	};

	const FTextSelection Selection = Inner->GetSelection();
	const FTextLocation SelBegin = Selection.GetBeginning();
	const FTextLocation SelEnd = Selection.GetEnd();
	const bool bMultiLine = Inner->AnyTextSelected() && SelBegin.GetLineIndex() != SelEnd.GetLineIndex();

	bSuppressCompletion = true;
	DismissCompletion();
	ON_SCOPE_EXIT { bSuppressCompletion = false; };

	if (bMultiLine)
	{
		// 多行选区：整体缩进 / 反缩进
		const int32 FirstLine = SelBegin.GetLineIndex();
		int32 LastLine = SelEnd.GetLineIndex();
		// 选区结束在行首时不处理该行
		if (SelEnd.GetOffset() == 0 && LastLine > FirstLine)
		{
			--LastLine;
		}

		TArray<FString> NewLines;
		for (int32 LineIdx = FirstLine; LineIdx <= LastLine; ++LineIdx)
		{
			const FString Line = GetLineText(LineIdx);
			if (bShift)
			{
				NewLines.Add(Unindent(Line));
			}
			else
			{
				NewLines.Add(Line.IsEmpty() ? Line : IndentUnit + Line);
			}
		}

		const FString LastOriginal = GetLineText(LastLine);
		{
			FScopedEditableTextTransaction Transaction(Inner);
			Inner->SelectText(FTextLocation(FirstLine, 0), FTextLocation(LastLine, LastOriginal.Len()));
			Inner->InsertTextAtCursor(FString::Join(NewLines, TEXT("\n")));
		}
		Inner->SelectText(FTextLocation(FirstLine, 0), FTextLocation(LastLine, NewLines.Last().Len()));
		return true;
	}

	if (bShift)
	{
		// 反缩进当前行
		const FTextLocation Cursor = Inner->GetCursorLocation();
		const FString Line = GetLineText(Cursor.GetLineIndex());
		const FString NewLine = Unindent(Line);
		const int32 Removed = Line.Len() - NewLine.Len();
		if (Removed > 0)
		{
			FScopedEditableTextTransaction Transaction(Inner);
			Inner->SelectText(FTextLocation(Cursor.GetLineIndex(), 0), FTextLocation(Cursor.GetLineIndex(), Removed));
			Inner->DeleteSelectedText();
			Inner->GoTo(FTextLocation(Cursor.GetLineIndex(), FMath::Max(0, Cursor.GetOffset() - Removed)));
		}
		return true;
	}

	Inner->InsertTextAtCursor(IndentUnit);
	return true;
}

// ====================================================================================================
// 代码补全
// ====================================================================================================

void STextFileEditor::RebuildWordIndex()
{
	bWordIndexDirty = false;
	WordIndex.Reset();

	if (CachedText.Len() > MaxWordIndexSizeKB * 1024)
	{
		return;
	}

	const int32 Len = CachedText.Len();
	int32 i = 0;
	while (i < Len)
	{
		const TCHAR C = CachedText[i];
		if (FTextFileSyntaxHighlighter::IsIdentifierChar(C) && !FChar::IsDigit(C))
		{
			const int32 Start = i;
			while (i < Len && FTextFileSyntaxHighlighter::IsIdentifierChar(CachedText[i]))
			{
				++i;
			}
			if (i - Start >= 3)
			{
				WordIndex.Add(CachedText.Mid(Start, i - Start));
			}
		}
		else
		{
			++i;
		}
	}
}

void STextFileEditor::TriggerCompletion(bool bManual)
{
	const UTextFileBrowserSettings* Settings = GetDefault<UTextFileBrowserSettings>();
	if (!Settings->bEnableCompletion && !bManual)
	{
		return;
	}

	const FString Prefix = GetWordBeforeCursor();
	if (Prefix.IsEmpty())
	{
		DismissCompletion();
		return;
	}

	if (bWordIndexDirty)
	{
		RebuildWordIndex();
		LastWordIndexTime = FSlateApplication::Get().GetCurrentTime();
	}

	UpdateCompletionCandidates(Prefix);
	if (CompletionItems.Num() == 0)
	{
		DismissCompletion();
		return;
	}

	if (IsCompletionOpen())
	{
		if (CompletionList.IsValid())
		{
			CompletionList->RequestListRefresh();
			CompletionSelectedIndex = 0;
			CompletionList->SetSelection(CompletionItems[0]);
		}
	}
	else
	{
		ShowCompletionPopup();
	}
}

void STextFileEditor::UpdateCompletionCandidates(const FString& Prefix)
{
	CompletionPrefix = Prefix;
	CompletionItems.Reset();

	const UTextFileBrowserSettings* Settings = GetDefault<UTextFileBrowserSettings>();
	const FTextFileLanguage* Lang = Highlighter.IsValid() ? Highlighter->GetLanguage() : nullptr;

	TSet<FString> Seen;
	TArray<FString> Exact;    // 大小写一致的前缀匹配排前面
	TArray<FString> Loose;

	auto Consider = [&](const FString& Word)
	{
		if (Word.Len() <= Prefix.Len() || Seen.Contains(Word))
		{
			return;
		}
		if (Word.StartsWith(Prefix, ESearchCase::CaseSensitive))
		{
			Seen.Add(Word);
			Exact.Add(Word);
		}
		else if (Word.StartsWith(Prefix, ESearchCase::IgnoreCase))
		{
			Seen.Add(Word);
			Loose.Add(Word);
		}
	};

	if (Lang)
	{
		for (const FString& Word : Lang->Keywords) { Consider(Word); }
		for (const FString& Word : Lang->Types) { Consider(Word); }
	}
	for (const FString& Word : WordIndex) { Consider(Word); }

	Exact.Sort();
	Loose.Sort();

	const int32 MaxItems = FMath::Max(3, Settings->CompletionMaxItems);
	for (const FString& Word : Exact)
	{
		if (CompletionItems.Num() >= MaxItems) { break; }
		CompletionItems.Add(MakeShared<FString>(Word));
	}
	for (const FString& Word : Loose)
	{
		if (CompletionItems.Num() >= MaxItems) { break; }
		CompletionItems.Add(MakeShared<FString>(Word));
	}
}

bool STextFileEditor::IsCompletionOpen() const
{
	return CompletionMenu.IsValid();
}

void STextFileEditor::ShowCompletionPopup()
{
	if (!TextBox.IsValid() || CompletionItems.Num() == 0)
	{
		return;
	}

	const UTextFileBrowserSettings* Settings = GetDefault<UTextFileBrowserSettings>();
	const FSlateFontInfo Font = GetEditorFont();
	const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const float RowHeight = FontMeasure->GetMaxCharacterHeight(Font) + 6.0f;
	const int32 VisibleRows = FMath::Min(CompletionItems.Num(), CompletionVisibleRows);

	CompletionSelectedIndex = 0;

	TSharedRef<SWidget> Content =
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Menu.Background"))
		.Padding(2.0f)
		[
			SNew(SBox)
			.HeightOverride(RowHeight * VisibleRows + 4.0f)
			.MinDesiredWidth(180.0f)
			[
				SAssignNew(CompletionList, SListView<TSharedPtr<FString>>)
				.ListItemsSource(&CompletionItems)
				.SelectionMode(ESelectionMode::Single)
				.OnGenerateRow(this, &STextFileEditor::GenerateCompletionRow)
				.OnMouseButtonClick(this, &STextFileEditor::HandleCompletionRowClicked)
			]
		];

	const FVector2D ScreenPos = ComputeCursorScreenPosition();
	CompletionMenu = FSlateApplication::Get().PushMenu(
		TextBox.ToSharedRef(),
		FWidgetPath(),
		Content,
		ScreenPos,
		FPopupTransitionEffect(FPopupTransitionEffect::TypeInPopup),
		/*bFocusImmediately*/ false);

	if (CompletionMenu.IsValid())
	{
		CompletionMenu->GetOnMenuDismissed().AddLambda([WeakThis = TWeakPtr<STextFileEditor>(SharedThis(this))](TSharedRef<IMenu>)
		{
			if (TSharedPtr<STextFileEditor> This = WeakThis.Pin())
			{
				This->CompletionMenu.Reset();
				This->CompletionList.Reset();
			}
		});
		CompletionList->SetSelection(CompletionItems[0]);
	}
}

void STextFileEditor::DismissCompletion()
{
	if (CompletionMenu.IsValid())
	{
		TSharedPtr<IMenu> Menu = CompletionMenu;
		CompletionMenu.Reset();
		CompletionList.Reset();
		Menu->Dismiss();
	}
}

void STextFileEditor::MoveCompletionSelection(int32 Delta)
{
	if (!CompletionList.IsValid() || CompletionItems.Num() == 0)
	{
		return;
	}
	CompletionSelectedIndex = FMath::Clamp(CompletionSelectedIndex + Delta, 0, CompletionItems.Num() - 1);
	CompletionList->SetSelection(CompletionItems[CompletionSelectedIndex]);
	CompletionList->RequestScrollIntoView(CompletionItems[CompletionSelectedIndex]);
}

void STextFileEditor::AcceptCompletion()
{
	if (!TextBox.IsValid() || !CompletionItems.IsValidIndex(CompletionSelectedIndex))
	{
		DismissCompletion();
		return;
	}

	const FString Word = *CompletionItems[CompletionSelectedIndex];
	DismissCompletion();

	TSharedPtr<SMultiLineEditableText> Inner = TextBox->GetEditableText();
	if (!Inner.IsValid())
	{
		return;
	}

	const FTextLocation Cursor = Inner->GetCursorLocation();
	const FString Prefix = GetWordBeforeCursor();

	bSuppressCompletion = true;
	{
		FScopedEditableTextTransaction Transaction(Inner);
		if (Prefix.Len() > 0)
		{
			Inner->SelectText(FTextLocation(Cursor.GetLineIndex(), Cursor.GetOffset() - Prefix.Len()), Cursor);
		}
		Inner->InsertTextAtCursor(Word);
	}
	bSuppressCompletion = false;
}

FVector2D STextFileEditor::ComputeCursorScreenPosition() const
{
	const FGeometry& Geometry = TextBox->GetCachedGeometry();
	const FVector2D LocalSize = Geometry.GetLocalSize();

	const FSlateFontInfo Font = GetEditorFont();
	const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const float LineHeight = FontMeasure->GetMaxCharacterHeight(Font);

	const FTextLocation Cursor = TextBox->GetCursorLocation();
	const int32 LineIndex = Cursor.IsValid() ? Cursor.GetLineIndex() : 0;

	// 文本框不暴露排版对象，这里用滚动条比例近似估算光标位置（不换行时准确）
	float ScrollY = 0.0f;
	if (TSharedPtr<const SScrollBar> VBar = TextBox->GetVScrollBar())
	{
		const float Thumb = VBar->ThumbSizeFraction();
		const float ContentHeight = Thumb > KINDA_SMALL_NUMBER ? LocalSize.Y / Thumb : LocalSize.Y;
		ScrollY = VBar->DistanceFromTop() * ContentHeight;
	}

	float ScrollX = 0.0f;
	if (TSharedPtr<const SScrollBar> HBar = TextBox->GetHScrollBar())
	{
		const float Thumb = HBar->ThumbSizeFraction();
		if (Thumb > KINDA_SMALL_NUMBER && Thumb < 1.0f - KINDA_SMALL_NUMBER)
		{
			ScrollX = HBar->DistanceFromTop() * (LocalSize.X / Thumb);
		}
	}

	const UTextFileBrowserSettings* Settings = GetDefault<UTextFileBrowserSettings>();
	FString Before = GetLineText(LineIndex).Left(Cursor.IsValid() ? Cursor.GetOffset() : 0);
	Before.ReplaceInline(TEXT("\t"), *FString::ChrN(FMath::Max(1, Settings->TabSize), TEXT(' ')));
	// 把光标放到当前前缀开头，让列表与正在输入的单词左对齐
	const int32 PrefixLen = CompletionPrefix.Len();
	if (PrefixLen > 0 && Before.Len() >= PrefixLen)
	{
		Before.LeftChopInline(PrefixLen);
	}

	const float TextX = FontMeasure->Measure(Before, Font).X;
	const float Padding = 6.0f;

	float LocalX = FMath::Clamp(TextX + Padding - ScrollX, 0.0f, FMath::Max(0.0f, LocalSize.X - 200.0f));
	float LocalY = FMath::Clamp(LineIndex * LineHeight + LineHeight + Padding - ScrollY, 0.0f, FMath::Max(0.0f, LocalSize.Y));

	return Geometry.LocalToAbsolute(FVector2D(LocalX, LocalY));
}

TSharedRef<ITableRow> STextFileEditor::GenerateCompletionRow(TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	const UTextFileBrowserSettings* Settings = GetDefault<UTextFileBrowserSettings>();
	const FTextFileLanguage* Lang = Highlighter.IsValid() ? Highlighter->GetLanguage() : nullptr;

	FLinearColor Color = Settings->HighlightColors.Normal;
	if (Lang)
	{
		const ESearchCase::Type SearchCase = Lang->bCaseSensitive ? ESearchCase::CaseSensitive : ESearchCase::IgnoreCase;
		auto InList = [&](const TArray<FString>& List)
		{
			for (const FString& W : List) { if (W.Equals(*Item, SearchCase)) { return true; } }
			return false;
		};
		if (InList(Lang->Keywords)) { Color = Settings->HighlightColors.Keyword; }
		else if (InList(Lang->Types)) { Color = Settings->HighlightColors.Type; }
	}

	// 用自定义行样式让选中色跟随配置
	static FTableRowStyle RowStyle;
	RowStyle = FAppStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row");
	RowStyle.SetActiveBrush(FSlateColorBrush(Settings->HighlightColors.CompletionSelection));
	RowStyle.SetInactiveBrush(FSlateColorBrush(Settings->HighlightColors.CompletionSelection));
	RowStyle.SetActiveHoveredBrush(FSlateColorBrush(Settings->HighlightColors.CompletionSelection));
	RowStyle.SetInactiveHoveredBrush(FSlateColorBrush(Settings->HighlightColors.CompletionSelection));

	return SNew(STableRow<TSharedPtr<FString>>, OwnerTable)
		.Style(&RowStyle)
		.Padding(FMargin(6.0f, 2.0f))
		[
			SNew(STextBlock)
			.Text(FText::FromString(*Item))
			.Font(GetEditorFont())
			.ColorAndOpacity(FSlateColor(Color))
		];
}

void STextFileEditor::HandleCompletionRowClicked(TSharedPtr<FString> Item)
{
	const int32 Index = CompletionItems.IndexOfByKey(Item);
	if (Index != INDEX_NONE)
	{
		CompletionSelectedIndex = Index;
		AcceptCompletion();
		FSlateApplication::Get().SetKeyboardFocus(TextBox);
	}
}

// ====================================================================================================
// 视图
// ====================================================================================================

FSlateFontInfo STextFileEditor::GetEditorFont() const
{
	return FCoreStyle::GetDefaultFontStyle("Mono", FontSize);
}

void STextFileEditor::ToggleWordWrap()
{
	bWordWrap = !bWordWrap;
	DismissCompletion();
	SaveViewPrefs();
}

void STextFileEditor::ToggleHighlight()
{
	bHighlightEnabled = !bHighlightEnabled;
	if (Highlighter.IsValid())
	{
		Highlighter->SetHighlightEnabled(bHighlightEnabled && !bHighlightForcedOff);
	}
	SaveViewPrefs();
}

void STextFileEditor::AdjustFontSize(int32 Delta)
{
	const int32 NewSize = FMath::Clamp(FontSize + Delta, 6, 48);
	if (NewSize == FontSize)
	{
		return;
	}
	FontSize = NewSize;
	DismissCompletion();
	ApplySettings();
	SaveViewPrefs();
}

void STextFileEditor::SaveViewPrefs() const
{
	if (GConfig)
	{
		GConfig->SetInt(ViewPrefsSection, TEXT("FontSize"), FontSize, GEditorPerProjectIni);
		GConfig->SetBool(ViewPrefsSection, TEXT("WordWrap"), bWordWrap, GEditorPerProjectIni);
		GConfig->SetBool(ViewPrefsSection, TEXT("Highlight"), bHighlightEnabled, GEditorPerProjectIni);
		GConfig->Flush(false, GEditorPerProjectIni);
	}
}

FText STextFileEditor::GetTabLabel() const
{
	const FString Name = FPaths::GetCleanFilename(Filename);
	return FText::FromString(bDirty ? Name + TEXT("*") : Name);
}

FText STextFileEditor::GetStatusText() const
{
	const TCHAR* EncodingName = TEXT("UTF-8");
	switch (Encoding)
	{
	case EFileEncoding::Utf8Bom: EncodingName = TEXT("UTF-8 BOM"); break;
	case EFileEncoding::Utf16LE: EncodingName = TEXT("UTF-16 LE"); break;
	case EFileEncoding::Utf16BE: EncodingName = TEXT("UTF-16 BE"); break;
	default: break;
	}

	int32 Line = 0;
	int32 Col = 0;
	if (TextBox.IsValid())
	{
		const FTextLocation Cursor = TextBox->GetCursorLocation();
		if (Cursor.IsValid())
		{
			Line = Cursor.GetLineIndex() + 1;
			Col = Cursor.GetOffset() + 1;
		}
	}

	return FText::Format(LOCTEXT("StatusFormat", "行 {0}, 列 {1}    {2}    {3}    {4} KB{5}"),
		FText::AsNumber(Line),
		FText::AsNumber(Col),
		FText::FromString(EncodingName),
		FText::FromString(bUseCRLF ? TEXT("CRLF") : TEXT("LF")),
		FText::AsNumber(FMath::DivideAndRoundUp<int64>(LoadedFileSize, 1024)),
		bDirty ? LOCTEXT("StatusModified", "    [已修改]") : FText::GetEmpty());
}

FText STextFileEditor::GetLanguageText() const
{
	const FTextFileLanguage* Lang = Highlighter.IsValid() ? Highlighter->GetLanguage() : nullptr;
	if (!Lang)
	{
		return LOCTEXT("PlainText", "纯文本");
	}
	if (bHighlightForcedOff)
	{
		return FText::Format(LOCTEXT("LangNoHighlightBig", "{0}（文件过大，已关闭高亮）"), FText::FromString(Lang->Name));
	}
	return FText::FromString(Lang->Name);
}

void STextFileEditor::OpenInExternalEditor() const
{
	FPlatformProcess::LaunchFileInDefaultExternalApplication(*Filename, nullptr, ELaunchVerb::Edit);
}

void STextFileEditor::ShowInExplorer() const
{
	FPlatformProcess::ExploreFolder(*Filename);
}

void STextFileEditor::Notify(const FText& InMessage, bool bSuccess)
{
	FNotificationInfo Info(InMessage);
	Info.ExpireDuration = bSuccess ? 3.0f : 6.0f;
	Info.bFireAndForget = true;
	if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
	{
		Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
	}
}

#undef LOCTEXT_NAMESPACE
