// Copyright 2026 Jot. All Rights Reserved.

// End-to-end check in a real editor. Run with -TabGroupsSelfTest=A (first session) then =B (after restart).
// Input goes through Slate's real input path with synthetic events; the OS cursor is not touched.
#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/SDockingTabStack.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Settings/EditorStyleSettings.h"
#include "HAL/FileManager.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Modules/ModuleManager.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "TabGroupsManager.h"
#include "TabGroupsSettings.h"
#include "TabGroupsState.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

DEFINE_LOG_CATEGORY_STATIC(LogTabGroupsSelfTest, Log, All);

namespace
{
	const TCHAR* const AssetPaths[] =
	{
		TEXT("/Engine/EditorMeshes/EditorCube.EditorCube"),
		TEXT("/Engine/EditorMeshes/EditorSphere.EditorSphere"),
		TEXT("/Engine/EditorMeshes/EditorPlane.EditorPlane"),
		TEXT("/Engine/BasicShapes/Cube.Cube"),
		TEXT("/Engine/BasicShapes/Sphere.Sphere"),
		TEXT("/Engine/BasicShapes/Cone.Cone"),
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"),
		TEXT("/Engine/BasicShapes/Plane.Plane"),
		TEXT("/Engine/EditorMeshes/EditorCylinder.EditorCylinder"),
		TEXT("/Engine/EngineMeshes/Cylinder.Cylinder"),
	};

	struct FStep
	{
		float Delay;
		TFunction<void()> Run;
	};

	class FSelfTest : public TSharedFromThis<FSelfTest>
	{
	public:
		void Start(const FString& InPhase)
		{
			Phase = InPhase;
			if (Phase == TEXT("A"))
			{
				BuildPhaseA();
			}
			else if (Phase == TEXT("B"))
			{
				BuildPhaseB();
			}
			else if (Phase == TEXT("I"))
			{
				BuildPhaseInnerPanels();
			}
			else if (Phase == TEXT("M"))
			{
				BuildPhaseMainWindow();
			}
			else
			{
				BuildPhaseCloseAll(Phase);
			}
			Handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSP(AsShared(), &FSelfTest::Tick), 0.25f);
		}

	private:
		// ---- helpers ----
		void Check(const TCHAR* Name, bool bOk)
		{
			UE_LOG(LogTabGroupsSelfTest, Display, TEXT("SELFTEST %s %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), Name);
			bOk ? ++Passed : ++Failed;
		}

		void Add(float Delay, TFunction<void()> Run)
		{
			Steps.Add({ Delay, MoveTemp(Run) });
		}

		static FTabGroupsManager& Manager() { return *FTabGroupsManager::Get(); }

		static TSharedPtr<SDockTab> AssetTab(int32 Index)
		{
			UAssetEditorSubsystem* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			UObject* Asset = LoadObject<UObject>(nullptr, AssetPaths[Index]);
			IAssetEditorInstance* Editor = Asset ? Editors->FindEditorForAsset(Asset, false) : nullptr;
			const TSharedPtr<FTabManager> TabManager = Editor ? Editor->GetAssociatedTabManager() : nullptr;
			return TabManager.IsValid() ? TabManager->GetOwnerTab() : nullptr;
		}

		static void OpenAsset(int32 Index)
		{
			if (UObject* Asset = LoadObject<UObject>(nullptr, AssetPaths[Index]))
			{
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Asset);
			}
		}

		TSharedPtr<FTabGroup> GroupNamed(const FString& Name) const
		{
			for (const TSharedRef<FTabGroup>& Group : Manager().GetAllGroups())
			{
				if (Group->Name == Name)
				{
					return Group;
				}
			}
			return nullptr;
		}

		FVector2D ChipCentre(const TSharedRef<SDockTab>&, const FString& Name) const
		{
			const TSharedPtr<FTabGroup> Group = GroupNamed(Name);
			return Group.IsValid() ? Manager().GetChipScreenPosition(Group->Id, FVector2D(0.5, 0.5)).Get(FVector2D::ZeroVector) : FVector2D::ZeroVector;
		}

		static FVector2D TabCentre(const TSharedRef<SDockTab>& Tab)
		{
			return FVector2D(Tab->GetTickSpaceGeometry().GetAbsolutePositionAtCoordinates(FVector2f(0.5f, 0.5f)));
		}

		void Mouse(int32 Kind, const FVector2D& Pos, FKey Button = EKeys::LeftMouseButton, const FModifierKeysState& Mods = FModifierKeysState())
		{
			FSlateApplication& App = FSlateApplication::Get();
			if (Kind == 0)
			{
				Pressed.Add(Button);
				App.ProcessMouseButtonDownEvent(nullptr, FPointerEvent(0u, Pos, LastMouse, Pressed, Button, 0.f, Mods));
			}
			else if (Kind == 1)
			{
				App.ProcessMouseMoveEvent(FPointerEvent(0u, Pos, LastMouse, Pressed, EKeys::Invalid, 0.f, Mods));
			}
			else
			{
				Pressed.Remove(Button);
				App.ProcessMouseButtonUpEvent(FPointerEvent(0u, Pos, Pos, Pressed, Button, 0.f, Mods));
			}
			LastMouse = Pos;
		}

		static FModifierKeysState CtrlDown() { return FModifierKeysState(false, false, true, false, false, false, false, false, false); }
		static FModifierKeysState ShiftDown() { return FModifierKeysState(true, false, false, false, false, false, false, false, false); }

		static void Raise(const TSharedPtr<SDockTab>& Tab)
		{
			if (const TSharedPtr<SWindow> Window = Tab.IsValid() ? Tab->GetParentWindow() : nullptr)
			{
				Window->HACK_ForceToFront();
				Window->BringToFront(true);
			}
		}

		void Click(const FVector2D& Pos, FKey Button = EKeys::LeftMouseButton, const FModifierKeysState& Mods = FModifierKeysState())
		{
			Mouse(1, Pos, Button, Mods);
			Mouse(0, Pos, Button, Mods);
			Mouse(2, Pos, Button, Mods);
		}

		void Shot(const FString& Name, TSharedPtr<SWindow> Window = nullptr)
		{
			if (!Window.IsValid())
			{
				const TSharedPtr<SDockTab> Tab = AssetTab(0);
				Window = Tab.IsValid() ? Tab->GetParentWindow() : nullptr;
			}
			if (!Window.IsValid())
			{
				const TArray<TSharedRef<SWindow>> Windows = FSlateApplication::Get().GetInteractiveTopLevelWindows();
				if (Windows.Num() == 0)
				{
					return;
				}
				Window = Windows[0];
			}
			TArray<FColor> Pixels;
			FIntVector Size;
			if (FSlateApplication::Get().TakeScreenshot(Window.ToSharedRef(), Pixels, Size))
			{
				for (FColor& Color : Pixels)
				{
					Color.A = 255;
				}
				const FString Path = FPaths::ProjectSavedDir() / TEXT("SelfTest") / (Phase + TEXT("_") + Name + TEXT(".png"));
				IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
				FImageUtils::SaveImageByExtension(*Path, FImageView(Pixels.GetData(), Size.X, Size.Y));
			}
		}

		// ---- scenarios ----
		void BuildPhaseA()
		{
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(AssetPaths); ++Index)
			{
				Add(0.5f, [Index]() { OpenAsset(Index); });
			}
			Add(3.f, [this]()
			{
				bool bAll = true;
				for (int32 Index = 0; Index < UE_ARRAY_COUNT(AssetPaths); ++Index)
				{
					bAll &= AssetTab(Index).IsValid();
				}
				Check(TEXT("all assets opened"), bAll);
				// Asset editors float by default in some configurations; the bar test needs them together.
				const TSharedPtr<SDockTab> Tab = AssetTab(0);
				Check(TEXT("asset tab in a well"), Tab.IsValid() && Tab->GetParent().IsValid());
			});
			Add(2.f, [this]() { Shot(TEXT("01_compact_tabs")); });

			Add(0.5f, [this]()
			{
				const TSharedPtr<FTabGroup> Materials = Manager().CreateGroup(AssetTab(0).ToSharedRef(), TEXT("Materials"), 4, false);
				Check(TEXT("create group"), Materials.IsValid());
				Manager().AddToGroup(AssetTab(1).ToSharedRef(), Materials->Id);
				Manager().AddToGroup(AssetTab(8).ToSharedRef(), Materials->Id); // Not adjacent: must be pulled together.
				const TSharedPtr<FTabGroup> Meshes = Manager().CreateGroup(AssetTab(3).ToSharedRef(), TEXT("Meshes"), 8, false);
				Manager().AddToGroup(AssetTab(4).ToSharedRef(), Meshes->Id);
				Manager().AddToGroup(AssetTab(5).ToSharedRef(), Meshes->Id);
				Check(TEXT("group members"), Materials->Tabs.Num() == 3 && Meshes->Tabs.Num() == 3);
			});
			Add(1.f, [this]()
			{
				const TSharedRef<SDockTab> First = AssetTab(0).ToSharedRef();
				const TSlotlessChildren<SDockTab>& Tabs = First->GetParentDockTabStack()->GetTabs();
				const int32 A = Tabs.Find(First);
				const int32 B = Tabs.Find(AssetTab(1).ToSharedRef());
				const int32 C = Tabs.Find(AssetTab(8).ToSharedRef());
				Check(TEXT("group tabs are adjacent"), FMath::Max3(A, B, C) - FMath::Min3(A, B, C) == 2);
				Shot(TEXT("02_two_groups"));
			});

			Add(0.5f, [this]() { Raise(AssetTab(0)); });

			// Real click on a real chip collapses the group.
			Add(0.5f, [this]() { Click(ChipCentre(AssetTab(3).ToSharedRef(), TEXT("Meshes"))); });
			Add(1.f, [this]()
			{
				const TSharedPtr<FTabGroup> Meshes = GroupNamed(TEXT("Meshes"));
				Check(TEXT("click chip collapses"), Meshes.IsValid() && Meshes->bCollapsed);
				const TSharedRef<SDockTab> Tab = AssetTab(4).ToSharedRef();
				Check(TEXT("collapsed tab stays alive in engine list"), Tab->GetParentDockTabStack()->GetTabs().Find(Tab) != INDEX_NONE);
				Shot(TEXT("03_meshes_collapsed"));
			});

			// Opening an asset in a collapsed group brings it forward and expands the group.
			Add(0.5f, [this]() { OpenAsset(4); });
			Add(1.5f, [this]()
			{
				const TSharedPtr<FTabGroup> Meshes = GroupNamed(TEXT("Meshes"));
				Check(TEXT("foregrounding a hidden tab expands its group"), Meshes.IsValid() && !Meshes->bCollapsed);
				Shot(TEXT("04_auto_expanded"));
			});

			// Collapsing a group holding the active tab moves the active tab out.
			Add(0.5f, [this]()
			{
				const TSharedPtr<FTabGroup> Meshes = GroupNamed(TEXT("Meshes"));
				Check(TEXT("collapse with active tab inside"), Manager().SetCollapsed(Meshes->Id, true));
			});
			Add(1.f, [this]()
			{
				const TSharedPtr<SDockTab> Tab = AssetTab(4);
				Check(TEXT("active tab moved out of collapsed group"), Tab.IsValid() && !Tab->IsForeground());
				Check(TEXT("group stays collapsed"), GroupNamed(TEXT("Meshes"))->bCollapsed);
			});

			// Pinning.
			Add(0.5f, [this]() { Manager().SetPinned(AssetTab(2).ToSharedRef(), true); });
			Add(1.f, [this]()
			{
				const TSharedRef<SDockTab> Tab = AssetTab(2).ToSharedRef();
				Check(TEXT("pinned"), Manager().IsPinned(Tab));
				Check(TEXT("pinned tab is not locked outside a menu"), !Tab->GetParentDockTabStack()->IsTabLocked(Tab));
				Check(TEXT("pinned tab hides its name"), Tab->IsTabNameHidden());
				Shot(TEXT("05_pinned"));
			});

			// Drag a loose tab into the middle of the expanded Materials group.
			Add(0.5f, [this]()
			{
				DragTab = AssetTab(6);
				const TSharedRef<SDockTab> Target = AssetTab(1).ToSharedRef();
				DragFrom = TabCentre(DragTab.ToSharedRef());
				DragTo = TabCentre(Target) + FVector2D(Target->GetTickSpaceGeometry().GetAbsoluteSize().X * 0.5f, 0.0);
				Mouse(1, DragFrom);
				Mouse(0, DragFrom);
			});
			Add(0.3f, [this]() { Mouse(1, DragFrom + FVector2D(-12, 0)); });
			Add(0.3f, [this]() { Mouse(1, FMath::Lerp(DragFrom, DragTo, 0.5)); });
			Add(0.3f, [this]() { Mouse(1, DragTo); });
			Add(0.5f, [this]() { Shot(TEXT("06_dragging_into_group")); });
			Add(0.3f, [this]() { Mouse(2, DragTo); });
			Add(1.f, [this]()
			{
				const TSharedPtr<FTabGroup> Group = Manager().FindGroupOfTab(DragTab.ToSharedRef());
				Check(TEXT("dragging a tab between members joins the group"), Group.IsValid() && Group->Name == TEXT("Materials"));
				Shot(TEXT("07_after_drag_in"));
			});

			// Drag it back out, past the end.
			Add(0.5f, [this]()
			{
				DragFrom = TabCentre(DragTab.ToSharedRef());
				const TSharedRef<SDockTab> Last = AssetTab(9).ToSharedRef();
				DragTo = TabCentre(Last) + FVector2D(Last->GetTickSpaceGeometry().GetAbsoluteSize().X, 0.0);
				Mouse(1, DragFrom);
				Mouse(0, DragFrom);
			});
			Add(0.3f, [this]() { Mouse(1, DragFrom + FVector2D(12, 0)); });
			Add(0.3f, [this]() { Mouse(1, DragTo); });
			Add(0.3f, [this]() { Mouse(2, DragTo); });
			Add(1.f, [this]()
			{
				Check(TEXT("dragging away leaves the group"), !Manager().FindGroupOfTab(DragTab.ToSharedRef()).IsValid());
			});

			// Real right click on a tab shows the menu with our entries.
			Add(0.5f, [this]() { Click(TabCentre(AssetTab(7).ToSharedRef()), EKeys::RightMouseButton); });
			Add(1.f, [this]()
			{
				Check(TEXT("tab context menu opens"), FSlateApplication::Get().AnyMenusVisible());
				const TSharedRef<SDockTab> PinnedTab = AssetTab(2).ToSharedRef();
				Check(TEXT("pinned tab is locked while a tab menu is open"), PinnedTab->GetParentDockTabStack()->IsTabLocked(PinnedTab));
				FSlateApplication::Get().DismissAllMenus();
			});

			Add(1.f, [this]()
			{
				const TSharedRef<SDockTab> PinnedTab = AssetTab(2).ToSharedRef();
				Check(TEXT("pinned tab unlocks when the menu closes"), !PinnedTab->GetParentDockTabStack()->IsTabLocked(PinnedTab));
			});

			// Real right click on a chip opens the editor popup.
			Add(0.5f, [this]() { Click(ChipCentre(AssetTab(0).ToSharedRef(), TEXT("Materials")), EKeys::RightMouseButton); });
			Add(1.f, [this]()
			{
				Check(TEXT("group editor opens"), FSlateApplication::Get().AnyMenusVisible());
				FSlateApplication::Get().DismissAllMenus();
			});

			// Popups don't appear in window captures, so render their content in a window of its own.
			Add(0.5f, [this]()
			{
				FMenuBuilder Builder(true, nullptr);
				Manager().FillTabContextMenu(Builder, AssetTab(0));
				Preview = SNew(SWindow).ClientSize(FVector2D(320, 160)).Title(FText::FromString(TEXT("Menu preview")))[Builder.MakeWidget()];
				FSlateApplication::Get().AddWindow(Preview.ToSharedRef());
			});
			Add(1.f, [this]()
			{
				Shot(TEXT("08_tab_menu_content"), Preview);
				Preview->RequestDestroyWindow();
				const TSharedPtr<FTabGroup> Group = GroupNamed(TEXT("Materials"));
				Preview = SNew(SWindow).ClientSize(FVector2D(360, 150)).Title(FText::FromString(TEXT("Editor preview")))[Manager().MakeGroupEditorWidget(Group->Id)];
				FSlateApplication::Get().AddWindow(Preview.ToSharedRef());
			});
			Add(1.f, [this]()
			{
				Shot(TEXT("09_group_editor_content"), Preview);
				Preview->RequestDestroyWindow();
				Preview.Reset();
			});

			// A group of one tab can be joined by dropping a tab onto it.
			Add(0.5f, [this]() { Raise(AssetTab(7)); Manager().CreateGroup(AssetTab(7).ToSharedRef(), TEXT("Solo"), 5, false); });
			Add(1.f, [this]()
			{
				DragTab = AssetTab(6);
				DragFrom = TabCentre(DragTab.ToSharedRef());
				DragTo = TabCentre(AssetTab(7).ToSharedRef());
				Mouse(1, DragFrom);
				Mouse(0, DragFrom);
			});
			Add(0.3f, [this]() { Mouse(1, DragFrom + FVector2D(DragTo.X > DragFrom.X ? 12 : -12, 0)); });
			Add(0.3f, [this]() { Mouse(1, FMath::Lerp(DragFrom, DragTo, 0.5)); });
			Add(0.3f, [this]() { Mouse(1, DragTo); });
			Add(0.5f, [this]() { Shot(TEXT("09e_dragging_onto_one_tab_group")); });
			Add(0.3f, [this]() { Mouse(2, DragTo); });
			Add(1.f, [this]()
			{
				const TSharedPtr<FTabGroup> Solo = GroupNamed(TEXT("Solo"));
				Check(TEXT("dropping onto a one-tab group joins it"), Solo.IsValid() && Solo->Tabs.Num() == 2 && Manager().FindGroupOfTab(DragTab.ToSharedRef()) == Solo);
				if (Solo.IsValid())
				{
					Manager().Ungroup(Solo->Id);
				}
				DragTab.Reset();
			});

			// Ctrl+click selects several tabs (the active one included); one menu action groups them all.
			Add(0.5f, [this]() { Raise(AssetTab(9)); Click(TabCentre(AssetTab(9).ToSharedRef())); });
			Add(0.5f, [this]() { Click(TabCentre(AssetTab(6).ToSharedRef()), EKeys::LeftMouseButton, CtrlDown()); });
			Add(0.5f, [this]() { Click(TabCentre(AssetTab(7).ToSharedRef()), EKeys::LeftMouseButton, CtrlDown()); });
			Add(0.5f, [this]()
			{
				const TArray<TSharedRef<SDockTab>> Selected = Manager().GetSelectedTabs();
				Check(TEXT("ctrl+click selects tabs plus the active one"), Selected.Num() == 3 && Selected.Contains(AssetTab(9).ToSharedRef()));
				Check(TEXT("ctrl+click doesn't switch the active tab"), AssetTab(9)->IsForeground());
				Shot(TEXT("09f_selected_tabs"));
				FMenuBuilder Builder(true, nullptr);
				Manager().FillTabContextMenu(Builder, AssetTab(6));
				Preview = SNew(SWindow).ClientSize(FVector2D(320, 140)).Title(FText::FromString(TEXT("Selection menu")))[Builder.MakeWidget()];
				FSlateApplication::Get().AddWindow(Preview.ToSharedRef());
			});
			Add(1.f, [this]()
			{
				Shot(TEXT("09g_selection_menu"), Preview);
				Preview->RequestDestroyWindow();
				Preview.Reset();
				const TSharedPtr<FTabGroup> Group = Manager().CreateGroupFromTabs(Manager().GetSelectedTabs());
				Check(TEXT("selected tabs become one group"), Group.IsValid() && Group->Tabs.Num() == 3);
				Check(TEXT("grouping clears the selection"), Manager().GetSelectedTabs().Num() == 0);
			});
			Add(1.f, [this]()
			{
				FSlateApplication::Get().DismissAllMenus();
				if (const TSharedPtr<FTabGroup> Group = Manager().FindGroupOfTab(AssetTab(6).ToSharedRef()))
				{
					Manager().Ungroup(Group->Id);
				}
			});
			Add(0.5f, [this]() { Click(TabCentre(AssetTab(6).ToSharedRef())); });
			Add(0.5f, [this]() { Click(TabCentre(AssetTab(9).ToSharedRef()), EKeys::LeftMouseButton, ShiftDown()); });
			Add(0.5f, [this]()
			{
				Check(TEXT("shift+click selects a range"), Manager().GetSelectedTabs().Num() >= 2);
				Click(TabCentre(AssetTab(7).ToSharedRef()));
			});
			Add(0.5f, [this]() { Check(TEXT("a plain click clears the selection"), Manager().GetSelectedTabs().Num() == 0); });

			// Dragging one selected tab brings the whole selection into the group it lands on.
			Add(0.5f, [this]() { Raise(AssetTab(9)); Click(TabCentre(AssetTab(9).ToSharedRef())); });
			Add(0.5f, [this]() { Click(TabCentre(AssetTab(6).ToSharedRef()), EKeys::LeftMouseButton, CtrlDown()); });
			Add(0.5f, [this]() { Click(TabCentre(AssetTab(7).ToSharedRef()), EKeys::LeftMouseButton, CtrlDown()); });
			Add(0.5f, [this]()
			{
				Check(TEXT("three tabs selected before the group drag"), Manager().GetSelectedTabs().Num() == 3);
				DragTab = AssetTab(6);
				DragFrom = TabCentre(DragTab.ToSharedRef());
				DragTo = TabCentre(AssetTab(0).ToSharedRef());
				Mouse(1, DragFrom);
				Mouse(0, DragFrom);
			});
			Add(0.2f, [this]() { Check(TEXT("pressing a selected tab keeps the selection"), Manager().GetSelectedTabs().Num() == 3); Mouse(1, DragFrom + FVector2D(-12, 0)); });
			Add(0.3f, [this]() { Mouse(1, FMath::Lerp(DragFrom, DragTo, 0.5)); });
			Add(0.3f, [this]() { Mouse(1, DragTo); });
			Add(0.5f, [this]() { Shot(TEXT("09h_dragging_selection")); });
			Add(0.3f, [this]() { Mouse(2, DragTo); });
			Add(1.f, [this]()
			{
				const TSharedPtr<FTabGroup> Materials = GroupNamed(TEXT("Materials"));
				Check(TEXT("all selected tabs joined the group"), Materials.IsValid() && Materials->Tabs.Num() == 6);
				Shot(TEXT("09i_after_selection_drag"));
				for (const int32 Index : { 6, 7, 9 })
				{
					Manager().RemoveFromGroup(AssetTab(Index).ToSharedRef());
				}
				DragTab.Reset();
			});
			Add(0.5f, [this]() { Check(TEXT("a drag clears the selection"), Manager().GetSelectedTabs().Num() == 0); });

			// Pressing and releasing a selected tab without dragging clears the selection.
			Add(0.5f, [this]() { Click(TabCentre(AssetTab(9).ToSharedRef())); });
			Add(0.5f, [this]() { Click(TabCentre(AssetTab(6).ToSharedRef()), EKeys::LeftMouseButton, CtrlDown()); });
			Add(0.5f, [this]() { Click(TabCentre(AssetTab(6).ToSharedRef())); });
			Add(0.5f, [this]() { Check(TEXT("a click on a selected tab clears the selection"), Manager().GetSelectedTabs().Num() == 0); });

			// The only tab of a group, dropped back on its own group, must not lose the group.
			Add(0.5f, [this]() { Manager().CreateGroup(AssetTab(7).ToSharedRef(), TEXT("Lone"), 5, false); });
			Add(1.f, [this]()
			{
				DragFrom = TabCentre(AssetTab(7).ToSharedRef());
				Mouse(1, DragFrom);
				Mouse(0, DragFrom);
			});
			Add(0.3f, [this]() { Mouse(1, DragFrom + FVector2D(12, 0)); });
			Add(0.3f, [this]() { Mouse(1, DragFrom + FVector2D(2, 0)); });
			Add(0.3f, [this]() { Mouse(2, DragFrom + FVector2D(2, 0)); });
			Add(1.f, [this]()
			{
				const TSharedPtr<FTabGroup> Lone = GroupNamed(TEXT("Lone"));
				Check(TEXT("a one-tab group survives dropping its tab back on it"), Lone.IsValid() && Lone->Tabs.Num() == 1);
				if (Lone.IsValid())
				{
					Manager().Ungroup(Lone->Id);
				}
			});

			// Colour wheel: a custom colour replaces the palette colour and survives saving.
			Add(0.5f, [this]()
			{
				const TSharedPtr<FTabGroup> Materials = GroupNamed(TEXT("Materials"));
				Manager().SetGroupCustomColor(Materials->Id, FLinearColor(FColor(0x12, 0xAB, 0x9F)));
				Check(TEXT("custom colour stored"), Materials->CustomColor == TEXT("12AB9F"));
			});
			Add(1.f, [this]() { Shot(TEXT("09b_custom_colour")); });

			// Close group keeps it in the menu; reopening brings the assets back as the same group.
			Add(0.5f, [this]()
			{
				const TSharedPtr<FTabGroup> Meshes = GroupNamed(TEXT("Meshes"));
				ClosedId = Meshes->Id.ToString();
				Manager().CloseGroup(Meshes->Id);
			});
			Add(1.5f, [this]()
			{
				Check(TEXT("closed group is gone from the bar"), !GroupNamed(TEXT("Meshes")).IsValid());
				Check(TEXT("closed group tabs closed"), !AssetTab(3).IsValid() && !AssetTab(4).IsValid() && !AssetTab(5).IsValid());
				const TArray<TabGroups::FSavedGroup>& Closed = Manager().GetClosedGroups();
				Check(TEXT("closed group listed"), Closed.Num() > 0 && Closed[0].Name == TEXT("Meshes") && Closed[0].Members.Num() == 3);
				Preview = SNew(SWindow).ClientSize(FVector2D(320, 260)).Title(FText::FromString(TEXT("Tab Groups menu")))[Manager().MakeTabGroupsMenuWidget()];
				FSlateApplication::Get().AddWindow(Preview.ToSharedRef());
			});
			Add(1.f, [this]()
			{
				Shot(TEXT("09c_tab_groups_menu"), Preview);
				Preview->RequestDestroyWindow();
				Preview.Reset();
				Manager().ReopenClosedGroup(ClosedId);
			});
			Add(5.f, [this]()
			{
				const TSharedPtr<FTabGroup> Meshes = GroupNamed(TEXT("Meshes"));
				Check(TEXT("reopened group is back with its tabs"), Meshes.IsValid() && Meshes->Tabs.Num() == 3);
				Check(TEXT("reopened group keeps its colour"), Meshes.IsValid() && Meshes->Color == 8);
				Check(TEXT("reopened group leaves the closed list"), !Manager().GetClosedGroups().ContainsByPredicate([this](const TabGroups::FSavedGroup& Saved) { return Saved.Id == ClosedId; }));
				if (Meshes.IsValid())
				{
					Manager().SetCollapsed(Meshes->Id, true); // The rest of the test expects it collapsed.
				}
				Shot(TEXT("09d_reopened"));
			});

			// Delete group closes the tabs and keeps nothing.
			Add(0.5f, [this]()
			{
				const TSharedPtr<FTabGroup> Temp = Manager().CreateGroup(AssetTab(9).ToSharedRef(), TEXT("Temp"), 1, false);
				Manager().DeleteGroup(Temp->Id);
			});
			Add(1.f, [this]()
			{
				Check(TEXT("deleted group tab closed"), !AssetTab(9).IsValid());
				Check(TEXT("deleted group not kept"), !Manager().GetClosedGroups().ContainsByPredicate([](const TabGroups::FSavedGroup& Saved) { return Saved.Name == TEXT("Temp"); }));
				OpenAsset(9);
			});
			Add(2.f, [this]() { Check(TEXT("top menu entry registered"), UToolMenus::Get()->FindMenu("MainFrame.MainMenu") != nullptr); });

			// Workspaces.
			// Steps above open assets, and Unreal activates them a few frames later, which (by design) expands their
			// collapsed group. Collapse Meshes again right before saving, which is what the workspace checks rely on.
			Add(0.5f, [this]()
			{
				if (const TSharedPtr<FTabGroup> Meshes = GroupNamed(TEXT("Meshes")))
				{
					Manager().SetCollapsed(Meshes->Id, true);
				}
			});
			Add(1.f, [this]()
			{
				const TSharedPtr<FTabGroup> Meshes = GroupNamed(TEXT("Meshes"));
				Check(TEXT("meshes collapsed before saving the workspace"), Meshes.IsValid() && Meshes->bCollapsed);
				Manager().SaveWorkspace(TEXT("Everything"));
			});
			Add(0.5f, [this]()
			{
				Check(TEXT("workspace saved"), Manager().GetWorkspaceNames().Contains(TEXT("Everything")));
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllAssetEditors();
			});
			Add(1.5f, [this]()
			{
				OpenAsset(7);
			});
			Add(2.f, [this]()
			{
				Check(TEXT("groups gone after closing all"), GroupNamed(TEXT("Materials")) == nullptr);
				Manager().SaveWorkspace(TEXT("Just plane"));
			});
			Add(1.f, [this]() { Manager().SwitchWorkspace(TEXT("Everything")); });
			Add(6.f, [this]()
			{
				Check(TEXT("switching brings groups back"), GroupNamed(TEXT("Materials")).IsValid() && GroupNamed(TEXT("Meshes")).IsValid());
				const TSharedPtr<FTabGroup> Materials = GroupNamed(TEXT("Materials"));
				Check(TEXT("expanded group reopened its assets"), Materials.IsValid() && Materials->Tabs.Num() == 3);
				const TSharedPtr<FTabGroup> Meshes = GroupNamed(TEXT("Meshes"));
				Check(TEXT("collapsed group waits unopened"), Meshes.IsValid() && Meshes->bCollapsed && Meshes->UnopenedKeys.Num() == 3 && Meshes->Tabs.Num() == 0);
				const TSharedPtr<SDockTab> PinnedTab = AssetTab(2);
				Check(TEXT("switching restores pinned tab"), PinnedTab.IsValid() && Manager().IsPinned(PinnedTab.ToSharedRef()));
				Shot(TEXT("12_workspace_restored"));
			});

			// Leave through the real close path; phase B checks what was remembered.
			Add(3.f, [this]() { Finish(true); });
		}

		void BuildPhaseB()
		{
			Add(14.f, [this]()
			{
				const TSharedPtr<FTabGroup> Materials = GroupNamed(TEXT("Materials"));
				Check(TEXT("restart: expanded group restored"), Materials.IsValid());
				Check(TEXT("restart: expanded group has its tabs"), Materials.IsValid() && Materials->Tabs.Num() == 3);
				Check(TEXT("restart: colour kept"), Materials.IsValid() && Materials->Color == 4);
				Check(TEXT("restart: custom colour kept"), Materials.IsValid() && Materials->CustomColor == TEXT("12AB9F"));
				const TSharedPtr<FTabGroup> Meshes = GroupNamed(TEXT("Meshes"));
				Check(TEXT("restart: collapsed group restored lazily"), Meshes.IsValid() && Meshes->bCollapsed && Meshes->UnopenedKeys.Num() == 3);
				const TSharedPtr<SDockTab> PinnedTab = AssetTab(2);
				Check(TEXT("restart: pinned tab restored"), PinnedTab.IsValid() && Manager().IsPinned(PinnedTab.ToSharedRef()));
				Check(TEXT("restart: workspaces kept"), Manager().GetWorkspaceNames().Num() == 2);
				Shot(TEXT("01_after_restart"));
			});
			Add(0.5f, [this]()
			{
				const TSharedPtr<FTabGroup> Meshes = GroupNamed(TEXT("Meshes"));
				if (Meshes.IsValid())
				{
					Manager().SetCollapsed(Meshes->Id, false);
				}
			});
			Add(4.f, [this]()
			{
				const TSharedPtr<FTabGroup> Meshes = GroupNamed(TEXT("Meshes"));
				Check(TEXT("restart: expanding opens the waiting assets"), Meshes.IsValid() && Meshes->Tabs.Num() == 3 && Meshes->UnopenedKeys.Num() == 0);
				Shot(TEXT("02_lazy_group_opened"));
			});
			Add(1.f, [this]() { Finish(false); });
		}

		// Inner panel bars, with nothing covering them.
		void BuildPhaseInnerPanels()
		{
			Add(0.5f, [this]()
			{
				const TSharedPtr<SDockTab> Details = FindPanel(TEXT("LevelEditorSelectionDetails"));
				Check(TEXT("found details panel"), Details.IsValid());
				const TSharedPtr<FTabManager> TabManager = Details->GetTabManagerPtr();
				for (int32 Index = 0; Index < 7; ++Index)
				{
					TSharedRef<SDockTab> Tab = SNew(SDockTab).TabRole(ETabRole::PanelTab).Label(FText::FromString(FString::Printf(TEXT("Panel %d"), Index)))
						[SNew(STextBlock).Text(FText::FromString(TEXT("test panel")))];
					TabManager->InsertNewDocumentTab(Details->GetLayoutIdentifier().TabType, FTabManager::FLiveTabSearch(Details->GetLayoutIdentifier().TabType), Tab);
					Panels.Add(Tab);
				}
			});
			Add(1.f, [this]()
			{
				const TSharedPtr<FTabGroup> Group = Manager().CreateGroup(Panels[1].ToSharedRef(), TEXT("Debug"), 2, false);
				Manager().AddToGroup(Panels[2].ToSharedRef(), Group->Id);
				Manager().AddToGroup(Panels[3].ToSharedRef(), Group->Id);
				const TSharedPtr<FTabGroup> Other = Manager().CreateGroup(Panels[4].ToSharedRef(), TEXT("Stats"), 7, false);
				Manager().AddToGroup(Panels[5].ToSharedRef(), Other->Id);
			});
			Add(1.f, [this]()
			{
				const FTabWellState* State = Manager().FindWellState(Panels[0]->GetParent().Get());
				Check(TEXT("panel bar wraps to two rows"), State && State->NumRows == 2);
				Shot(TEXT("01_panels_expanded"), Panels[0]->GetParentWindow());
			});
			Add(0.5f, [this]() { Click(ChipCentre(Panels[1].ToSharedRef(), TEXT("Debug"))); });
			Add(1.f, [this]()
			{
				const TSharedPtr<FTabGroup> Group = GroupNamed(TEXT("Debug"));
				Check(TEXT("panel chip click collapses"), Group.IsValid() && Group->bCollapsed);
				Shot(TEXT("02_panels_collapsed"), Panels[0]->GetParentWindow());
			});
			Add(0.5f, [this]() { Click(ChipCentre(Panels[1].ToSharedRef(), TEXT("Debug"))); });
			Add(1.f, [this]()
			{
				const TSharedPtr<FTabGroup> Group = GroupNamed(TEXT("Debug"));
				Check(TEXT("panel chip click expands"), Group.IsValid() && !Group->bCollapsed);
				for (const TSharedPtr<SDockTab>& Tab : Panels)
				{
					Tab->RequestCloseTab();
				}
				Panels.Empty();
			});
			Add(1.f, [this]() { Check(TEXT("closing panels removes groups"), Manager().GetAllGroups().Num() == 0); Finish(false); });
		}

		// Asset tabs docked in the main window, where the tab bar shares the title area.
		void BuildPhaseMainWindow()
		{
			Add(0.5f, []() { GetMutableDefault<UEditorStyleSettings>()->AssetEditorOpenLocation = EAssetEditorOpenLocation::MainWindow; });
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(AssetPaths); ++Index)
			{
				Add(0.5f, [Index]() { OpenAsset(Index); });
			}
			Add(3.f, [this]()
			{
				const TSharedPtr<SDockTab> LevelEditor = FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId("LevelEditor"));
				Check(TEXT("assets docked in the main window"), AssetTab(0)->GetParent() == LevelEditor->GetParent());
				Shot(TEXT("01_main_window_tabs"));
			});
			Add(0.5f, [this]()
			{
				const TSharedPtr<FTabGroup> Shapes = Manager().CreateGroup(AssetTab(3).ToSharedRef(), TEXT("Shapes"), 1, false);
				Manager().AddToGroup(AssetTab(4).ToSharedRef(), Shapes->Id);
				Manager().AddToGroup(AssetTab(5).ToSharedRef(), Shapes->Id);
				Manager().AddToGroup(AssetTab(6).ToSharedRef(), Shapes->Id);
				const TSharedPtr<FTabGroup> Editor = Manager().CreateGroup(AssetTab(0).ToSharedRef(), TEXT("Editor meshes"), 6, false);
				Manager().AddToGroup(AssetTab(1).ToSharedRef(), Editor->Id);
				Manager().SetPinned(AssetTab(9).ToSharedRef(), true);
			});
			Add(1.f, [this]() { Shot(TEXT("02_main_window_groups")); });
			Add(0.5f, [this]() { Click(ChipCentre(AssetTab(3).ToSharedRef(), TEXT("Shapes"))); });
			Add(1.f, [this]()
			{
				const TSharedPtr<FTabGroup> Shapes = GroupNamed(TEXT("Shapes"));
				Check(TEXT("main window: chip click collapses"), Shapes.IsValid() && Shapes->bCollapsed);
				Shot(TEXT("03_main_window_collapsed"));
			});
			Add(0.5f, [this]() { Click(ChipCentre(AssetTab(3).ToSharedRef(), TEXT("Shapes"))); });
			Add(1.f, [this]()
			{
				const TSharedPtr<FTabGroup> Shapes = GroupNamed(TEXT("Shapes"));
				Check(TEXT("main window: chip click expands"), Shapes.IsValid() && !Shapes->bCollapsed);
			});

			// Drag a grouped tab out of the bar into its own window.
			Add(0.5f, [this]()
			{
				DragTab = AssetTab(4);
				DragFrom = TabCentre(DragTab.ToSharedRef());
				DragTo = DragFrom + FVector2D(120, 400);
				Mouse(1, DragFrom);
				Mouse(0, DragFrom);
			});
			Add(0.3f, [this]() { Mouse(1, DragFrom + FVector2D(0, 14)); });
			Add(0.3f, [this]() { Mouse(1, DragFrom + FVector2D(60, 200)); });
			Add(0.3f, [this]() { Mouse(1, DragTo); });
			Add(0.3f, [this]() { Mouse(2, DragTo); });
			Add(1.5f, [this]()
			{
				Check(TEXT("tab dragged to a new window left its group"), !Manager().FindGroupOfTab(DragTab.ToSharedRef()).IsValid());
				const TSharedPtr<FTabGroup> Shapes = GroupNamed(TEXT("Shapes"));
				Check(TEXT("the rest of the group stayed"), Shapes.IsValid() && Shapes->Tabs.Num() == 3);
				DragTab.Reset();
				Shot(TEXT("04_main_window_after_drag_out"));
			});

			// Leave with groups on the main bar: unhooking and widget teardown must be clean.
			Add(1.f, []() { GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllAssetEditors(); });
			Add(3.f, [this]()
			{
				Check(TEXT("main window: close all clears groups"), Manager().GetAllGroups().Num() == 0);
				Finish(false);
			});
		}

		// Baseline: does closing every asset editor at once survive, with or without this plugin (-NoTabGroups)?
		void BuildPhaseCloseAll(const FString& Variant)
		{
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(AssetPaths); ++Index)
			{
				Add(0.5f, [Index]() { OpenAsset(Index); });
			}
			Add(2.f, [Variant]()
			{
				if (Variant == TEXT("C") || Variant == TEXT("Q"))
				{
					return;
				}
				if (Variant == TEXT("P"))
				{
					Manager().SetPinned(AssetTab(2).ToSharedRef(), true);
					return;
				}
				const TSharedPtr<FTabGroup> Meshes = Manager().CreateGroup(AssetTab(3).ToSharedRef(), TEXT("Meshes"), 8, false);
				Manager().AddToGroup(AssetTab(4).ToSharedRef(), Meshes->Id);
				Manager().AddToGroup(AssetTab(5).ToSharedRef(), Meshes->Id);
				if (Variant == TEXT("H"))
				{
					Manager().SetCollapsed(Meshes->Id, true);
				}
			});
			if (Variant == TEXT("Q"))
			{
				Add(3.f, [this]() { Check(TEXT("quit with editors open"), true); Finish(true); });
				return;
			}
			Add(3.f, []() { GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllAssetEditors(); });
			Add(3.f, [this]() { Check(TEXT("close all survived"), true); Finish(false); });
		}

		TSharedPtr<SDockTab> FindPanel(const TCHAR* TypeName)
		{
			const TSharedPtr<SDockTab> LevelEditor = FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId("LevelEditor"));
			if (!LevelEditor.IsValid())
			{
				return nullptr;
			}
			LevelEditor->ActivateInParent(ETabActivationCause::SetDirectly);
			const TSharedPtr<FTabManager> Inner = FGlobalTabmanager::Get()->GetTabManagerForMajorTab(LevelEditor);
			return Inner.IsValid() ? Inner->FindExistingLiveTab(FTabId(TypeName)) : nullptr;
		}

		void Finish(bool bCloseEditorNormally)
		{
			UE_LOG(LogTabGroupsSelfTest, Display, TEXT("SELFTEST DONE phase=%s passed=%d failed=%d"), *Phase, Passed, Failed);
			Panels.Empty();
			DragTab.Reset();
			FTSTicker::RemoveTicker(Handle);
			if (bCloseEditorNormally)
			{
				GEditor->Exec(nullptr, TEXT("QUIT_EDITOR"));
			}
			else
			{
				FPlatformMisc::RequestExit(false);
			}
		}

		bool Tick(float)
		{
			if (!GEditor)
			{
				return true;
			}
			if (!bStarted)
			{
				const TSharedPtr<SDockTab> LevelEditor = FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId("LevelEditor"));
				if (!LevelEditor.IsValid())
				{
					return true;
				}
				bStarted = true;
				NextTime = FPlatformTime::Seconds() + 3.0;
			}
			if (FPlatformTime::Seconds() < NextTime)
			{
				return true;
			}
			if (Next >= Steps.Num())
			{
				return true;
			}
			const FStep& Step = Steps[Next];
			if (!bDelayDone)
			{
				bDelayDone = true;
				NextTime = FPlatformTime::Seconds() + Step.Delay;
				return true;
			}
			bDelayDone = false;
			++Next;
			Step.Run();
			return true;
		}

		FString Phase;
		TArray<FStep> Steps;
		int32 Next = 0;
		double NextTime = 0.0;
		bool bStarted = false;
		bool bDelayDone = false;
		int32 Passed = 0;
		int32 Failed = 0;
		FTSTicker::FDelegateHandle Handle;
		TSet<FKey> Pressed;
		FVector2D LastMouse = FVector2D::ZeroVector;
		TSharedPtr<SDockTab> DragTab;
		FVector2D DragFrom = FVector2D::ZeroVector;
		FVector2D DragTo = FVector2D::ZeroVector;
		TArray<TSharedPtr<SDockTab>> Panels;
		TSharedPtr<SWindow> Preview;
		FString ClosedId;
	};

	TSharedPtr<FSelfTest> GSelfTest;

}

class FTabGroupsTestsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FString Phase;
		if (FParse::Value(FCommandLine::Get(), TEXT("TabGroupsSelfTest="), Phase))
		{
			GSelfTest = MakeShared<FSelfTest>();
			GSelfTest->Start(Phase);
		}
	}

	virtual void ShutdownModule() override
	{
		GSelfTest.Reset();
	}
};

IMPLEMENT_MODULE(FTabGroupsTestsModule, TabGroupsTests)
