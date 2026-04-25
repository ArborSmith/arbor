#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/STextComboBox.h"
#include "Styling/SlateTypes.h"
#include "HAL/PlatformProcess.h"

class SScrollBox;
class SMultiLineEditableTextBox;
class SRichTextBlock;
class STextBlock;
class SButton;
class SBorder;
class SCheckBox;
class SHorizontalBox;
class SOverlay;
class SVerticalBox;

enum class EArborMessageType : uint8
{
	User,
	Assistant,
	Tool,
	System
};

struct FArborChatMessage
{
	EArborMessageType Type;
	FString Text;
	TSharedPtr<SRichTextBlock> TextWidget;
};

class SArborChatWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SArborChatWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SArborChatWidget() override;

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/**
	 * Send a message through the existing Claude process from an external widget.
	 * Starts Claude if not already running. The message appears in the chat as a user message.
	 */
	void SendExternalMessage(const FString& Message);

	/**
	 * Send a message in plan mode. Switches to plan mode (restarting the process if needed),
	 * then sends the message. Used by the "Help Implement" button on Features.
	 */
	void SendExternalMessageInPlanMode(const FString& Message);

	/** Returns true if the Claude subprocess is currently running. */
	bool IsClaudeRunning() const { return bProcessRunning; }

	/** Returns true if Claude is currently processing a request. */
	bool IsClaudeWaiting() const { return bWaitingForResponse; }

private:
	// Process management
	void StartClaudeProcess(const FString& ResumeSessionId = TEXT(""));
	void CleanUpProcess();
	void SendUserMessage(const FString& Message);
	void SendToolResult(const FString& ToolUseId, const FString& Content);
	void WriteToPipe(const FString& Json);
	void ProcessJsonLine(const FString& Line);
	void HandleAssistantMessage(const TSharedPtr<FJsonObject>& JsonObj);
	void HandleAssistantTextBlock(const TSharedPtr<FJsonObject>& ContentObj);
	void HandleToolUseBlock(const TSharedPtr<FJsonObject>& ContentObj);
	void HandleAskUserQuestion(const FString& ToolUseId, const TSharedPtr<FJsonObject>& ContentObj);
	void HandleExitPlanMode(const FString& ToolUseId, const TSharedPtr<FJsonObject>& ContentObj);
	void OnPlanApprovalClicked(bool bApproved);
	void HandleUserMessage(const TSharedPtr<FJsonObject>& JsonObj);
	void HandleResultMessage(const TSharedPtr<FJsonObject>& JsonObj);
	void OnSendClicked();
	void OnQuestionOptionClicked(const FString& OptionLabel);
	FReply OnInputKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent);
	void SetStatus(const FString& Text);

	// Message system
	void AddMessage(EArborMessageType Type, const FString& Text);
	void AppendToCurrentMessage(const FString& Text);
	void FinalizeCurrentMessage();
	TSharedRef<SWidget> CreateMessageWidget(FArborChatMessage& Message);
	TSharedRef<SWidget> CreateToolPillWidget(const FString& DisplayText);
	void GetMessageStyle(EArborMessageType Type, FLinearColor& OutBarColor, FLinearColor& OutLabelColor, FLinearColor& OutTextColor, FLinearColor& OutBgColor, FString& OutLabel) const;

	// Rich text helpers
	static FString EscapeRichText(const FString& Text);
	static FString MarkupUrlsInRichText(const FString& RawText);

	// Toolbar actions
	void OnNewSessionClicked();
	void OnRestartClicked();
	void InterruptClaude();
	void ClearChat();
	void UpdateConnectionStatus();
	// Screenshot viewer
	static bool ExtractImagePath(const FString& Text, FString& OutPath);
	void OpenImageWindow(const FString& ImagePath);

	// Session persistence
	static FString GetSessionFilePath();
	void SaveSessionToFile() const;
	FString LoadSessionFromFile() const;

	// Subprocess handles
	FProcHandle ClaudeProcess;
	uint32 ProcessId = 0;
	void* StdOutReadPipe = nullptr;
	void* StdOutWritePipe = nullptr;
	void* StdInReadPipe = nullptr;
	void* StdInWritePipe = nullptr;

	// UI elements
	TSharedPtr<SScrollBox> OutputScrollBox;
	TSharedPtr<SMultiLineEditableTextBox> InputTextBox;
	TSharedPtr<STextBlock> StatusTextBlock;
	TSharedPtr<SButton> SendButton;
	TSharedPtr<SButton> StopButton;
	TSharedPtr<SBorder> ConnectionStatusDot;
	TSharedPtr<STextComboBox> ModelComboBox;
	TSharedPtr<SCheckBox> DebugCheckBox;
	TSharedPtr<SCheckBox> PlanCheckBox;
	TSharedPtr<SBorder> ModeIndicatorBorder;
	TSharedPtr<STextBlock> ModeIndicatorText;
	TSharedPtr<SHorizontalBox> QuestionButtonsBox;

	// Scroll-to-bottom overlay
	TSharedPtr<SButton> ScrollToBottomButton;

	// Status area
	TSharedPtr<SBorder> StatusDot;
	TSharedPtr<STextBlock> StatusConnectionLabel;
	TSharedPtr<SBorder> ConnectionStatusBadge;

	// Fonts (cached for reuse in CreateMessageWidget)
	FSlateFontInfo MonoFont;
	FSlateFontInfo BodyFont;
	FSlateFontInfo LabelFont;
	FSlateFontInfo SmallFont;

	// Rich text styles per message type (persisted for SRichTextBlock lifetime)
	FTextBlockStyle RichTextStyleUser;
	FTextBlockStyle RichTextStyleAssistant;
	FTextBlockStyle RichTextStyleSystem;

	// Model selector
	TArray<TSharedPtr<FString>> ModelOptions;
	TSharedPtr<FString> SelectedModel;

	// Message state
	TArray<FArborChatMessage> Messages;
	int32 CurrentStreamingIndex = INDEX_NONE;

	// Session (for resume after interrupt)
	FString SessionId;

	// Process state
	FString StdOutLineBuffer;
	FString CurrentToolName;
	bool bProcessRunning = false;
	bool bInitialized = false;
	bool bNeedsSendInit = false;
	bool bWaitingForResponse = false;
	bool bAutoScroll = true;
	bool bNeedsScroll = false;
	TSet<FString> ShownImageLinks;
	TSet<FString> ProcessedToolUseIds;
	bool bIsAutoScrolling = false;
	bool bDebugMode = false;
	bool bPlanMode = false;
	bool bSuppressAutoResolve = false;
	bool bPendingPlanExecution = false;
	FString PlanFilePath;

	// Status animation
	double StatusAnimTime = 0.0;

	// Pending question option descriptions (parallel to PendingQuestionOptions)
	TArray<FString> PendingQuestionDescriptions;

	// Pending AskUserQuestion
	FString PendingToolUseId;
	FString PendingQuestionText;
	TArray<FString> PendingQuestionOptions;
	FString PendingQuestionsJson;
};
