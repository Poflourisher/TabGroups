// Copyright 2026 Jot. All Rights Reserved.

#include "TabWellHook.h"
#include "TabGroupsLog.h"
#include "TabGroupsManager.h"
#include "TabWellAccess.h"
#include "Input/DragAndDrop.h"
#include "Layout/ArrangedChildren.h"
#include "Widgets/SPanel.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"

namespace
{
	// ---- Finding vtable slots ----
	// Each probe overrides exactly one function of a shared base. The slot that differs from the
	// base in that probe, and in no other probe, belongs to that function. This asks the compiler
	// instead of hard-coding numbers, so it stays correct when the engine adds virtual functions.
	volatile int32 GProbeSink = 0;

	class SProbeBase : public SPanel
	{
	public:
		SLATE_BEGIN_ARGS(SProbeBase) {}
		SLATE_END_ARGS()
		void Construct(const FArguments&) {}
		virtual void OnArrangeChildren(const FGeometry&, FArrangedChildren&) const override { GProbeSink = 1; }
		virtual FVector2D ComputeDesiredSize(float) const override { GProbeSink = 2; return FVector2D(1, 1); }
		virtual FChildren* GetChildren() override { return &FNoChildren::NoChildrenInstance; }
		virtual FReply OnDrop(const FGeometry&, const FDragDropEvent&) override { GProbeSink = 3; return FReply::Unhandled(); }
		virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override { GProbeSink = 4; return FReply::Unhandled(); }
		virtual FReply OnDragOver(const FGeometry&, const FDragDropEvent&) override { GProbeSink = 5; return FReply::Unhandled(); }
	};

#define TABGROUPS_PROBE(Name, Body) \
	class Name : public SProbeBase \
	{ \
	public: \
		SLATE_BEGIN_ARGS(Name) {} \
		SLATE_END_ARGS() \
		void Construct(const FArguments&) {} \
		Body \
	};

	TABGROUPS_PROBE(SProbeArrange, virtual void OnArrangeChildren(const FGeometry&, FArrangedChildren&) const override { GProbeSink = 11; })
	TABGROUPS_PROBE(SProbeDesired, virtual FVector2D ComputeDesiredSize(float) const override { GProbeSink = 12; return FVector2D(2, 2); })
	TABGROUPS_PROBE(SProbeDrop, virtual FReply OnDrop(const FGeometry&, const FDragDropEvent&) override { GProbeSink = 13; return FReply::Handled(); })
	TABGROUPS_PROBE(SProbeMouseUp, virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override { GProbeSink = 14; return FReply::Handled(); })
	TABGROUPS_PROBE(SProbeDragOver, virtual FReply OnDragOver(const FGeometry&, const FDragDropEvent&) override { GProbeSink = 15; return FReply::Handled(); })
#undef TABGROUPS_PROBE

	void** VTableOf(const void* Object)
	{
		return *reinterpret_cast<void** const*>(Object);
	}

	bool IsCode(const void* Address)
	{
		MEMORY_BASIC_INFORMATION Info;
		if (!Address || !VirtualQuery(Address, &Info, sizeof(Info)) || Info.State != MEM_COMMIT)
		{
			return false;
		}
		return (Info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
	}

	/** A vtable ends at the first entry that is not code: past it lies the next table's RTTI pointer. */
	int32 CountSlots(void** VTable, int32 Max)
	{
		int32 Count = 0;
		while (Count < Max && IsCode(VTable[Count]))
		{
			++Count;
		}
		return Count;
	}

	enum EHookSlot { Arrange, Desired, Drop, MouseUp, DragOver, NumHooks };

	bool FindSlots(int32 OutSlots[NumHooks])
	{
		TSharedRef<SProbeBase> Base = SNew(SProbeBase);
		TSharedRef<SProbeArrange> ProbeArrange = SNew(SProbeArrange);
		TSharedRef<SProbeDesired> ProbeDesired = SNew(SProbeDesired);
		TSharedRef<SProbeDrop> ProbeDrop = SNew(SProbeDrop);
		TSharedRef<SProbeMouseUp> ProbeMouseUp = SNew(SProbeMouseUp);
		TSharedRef<SProbeDragOver> ProbeDragOver = SNew(SProbeDragOver);

		void** BaseTable = VTableOf(&Base.Get());
		void** Tables[NumHooks] = { VTableOf(&ProbeArrange.Get()), VTableOf(&ProbeDesired.Get()), VTableOf(&ProbeDrop.Get()), VTableOf(&ProbeMouseUp.Get()), VTableOf(&ProbeDragOver.Get()) };

		int32 Count = CountSlots(BaseTable, 512);
		for (void** Table : Tables)
		{
			Count = FMath::Min(Count, CountSlots(Table, 512));
		}

		int32 Matches[NumHooks] = {};
		for (int32 Slot = 0; Slot < Count; ++Slot)
		{
			int32 Differs = 0;
			int32 Which = INDEX_NONE;
			for (int32 Hook = 0; Hook < NumHooks; ++Hook)
			{
				if (Tables[Hook][Slot] != BaseTable[Slot])
				{
					++Differs;
					Which = Hook;
				}
			}
			if (Differs == 1)
			{
				OutSlots[Which] = Slot;
				++Matches[Which];
			}
		}

		for (int32 Hook = 0; Hook < NumHooks; ++Hook)
		{
			if (Matches[Hook] != 1)
			{
				UE_LOG(LogTabGroups, Error, TEXT("Could not identify tab well function %d (%d matches)."), Hook, Matches[Hook]);
				return false;
			}
		}
		return true;
	}

	// ---- The hooks ----
	// These are member functions so that they share the calling convention of the real virtuals.
	// 'this' is really an SDockingTabWell.
	struct FWellHooks
	{
		const SDockingTabWell& Well() const { return *reinterpret_cast<const SDockingTabWell*>(this); }

		void HookArrange(const FGeometry& Geometry, FArrangedChildren& Out) const;
		FVector2D HookDesired(float Scale) const;
		FReply HookDrop(const FGeometry& Geometry, const FDragDropEvent& Event);
		FReply HookMouseUp(const FGeometry& Geometry, const FPointerEvent& Event);
		FReply HookDragOver(const FGeometry& Geometry, const FDragDropEvent& Event);
	};

	using FArrangeFn = void (FWellHooks::*)(const FGeometry&, FArrangedChildren&) const;
	using FDesiredFn = FVector2D (FWellHooks::*)(float) const;
	using FDropFn = FReply (FWellHooks::*)(const FGeometry&, const FDragDropEvent&);
	using FMouseUpFn = FReply (FWellHooks::*)(const FGeometry&, const FPointerEvent&);
	using FDragOverFn = FReply (FWellHooks::*)(const FGeometry&, const FDragDropEvent&);

	template <typename F>
	void* ToAddress(F Function)
	{
		static_assert(sizeof(F) == sizeof(void*));
		void* Address;
		FMemory::Memcpy(&Address, &Function, sizeof(Address));
		return Address;
	}

	template <typename F>
	F FromAddress(void* Address)
	{
		static_assert(sizeof(F) == sizeof(void*));
		F Function;
		FMemory::Memcpy(&Function, &Address, sizeof(Address));
		return Function;
	}

	void** GVTable = nullptr;
	int32 GSlots[NumHooks] = {};
	void* GOriginals[NumHooks] = {};

	void FWellHooks::HookArrange(const FGeometry& Geometry, FArrangedChildren& Out) const
	{
		FTabGroupsManager* Manager = FTabGroupsManager::Get();
		if (!Manager || !Manager->ArrangeWell(Well(), Geometry, Out))
		{
			(this->*FromAddress<FArrangeFn>(GOriginals[Arrange]))(Geometry, Out);
		}
	}

	FVector2D FWellHooks::HookDesired(float Scale) const
	{
		FVector2D Size = (this->*FromAddress<FDesiredFn>(GOriginals[Desired]))(Scale);
		if (FTabGroupsManager* Manager = FTabGroupsManager::Get())
		{
			Size.Y *= Manager->GetRowCount(Well());
		}
		return Size;
	}

	FReply FWellHooks::HookDrop(const FGeometry& Geometry, const FDragDropEvent& Event)
	{
		FTabGroupsManager* Manager = FTabGroupsManager::Get();
		TSharedPtr<SDockTab> Tab;
		int32 DropIndex = INDEX_NONE;
		if (Manager)
		{
			Manager->BeforeDrop(Well(), Tab, DropIndex);
		}
		const FReply Reply = (this->*FromAddress<FDropFn>(GOriginals[Drop]))(Geometry, Event);
		if (Manager && Reply.IsEventHandled())
		{
			Manager->AfterDrop(const_cast<SDockingTabWell&>(Well()), Tab, DropIndex);
		}
		return Reply;
	}

	FReply FWellHooks::HookMouseUp(const FGeometry& Geometry, const FPointerEvent& Event)
	{
		// Tabs that may not leave their well are reordered here instead of in OnDrop.
		FTabGroupsManager* Manager = FTabGroupsManager::Get();
		TSharedPtr<SDockTab> Tab;
		int32 DropIndex = INDEX_NONE;
		const bool bWasDragging = TabWellAccess::DraggedTab(Well()).IsValid();
		if (Manager && bWasDragging)
		{
			Manager->BeforeDrop(Well(), Tab, DropIndex);
		}
		const FReply Reply = (this->*FromAddress<FMouseUpFn>(GOriginals[MouseUp]))(Geometry, Event);
		if (Manager && bWasDragging && Reply.IsEventHandled())
		{
			Manager->AfterDrop(const_cast<SDockingTabWell&>(Well()), Tab, DropIndex);
		}
		return Reply;
	}

	FReply FWellHooks::HookDragOver(const FGeometry& Geometry, const FDragDropEvent& Event)
	{
		if (FTabGroupsManager* Manager = FTabGroupsManager::Get())
		{
			Manager->OnDragMoved(Well(), FVector2D(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition())));
		}
		return (this->*FromAddress<FDragOverFn>(GOriginals[DragOver]))(Geometry, Event);
	}

	bool WriteSlots(void* const Values[NumHooks])
	{
		// Slots can span two pages, so protect the whole range they occupy.
		int32 First = GSlots[0];
		int32 Last = GSlots[0];
		for (int32 Hook = 1; Hook < NumHooks; ++Hook)
		{
			First = FMath::Min(First, GSlots[Hook]);
			Last = FMath::Max(Last, GSlots[Hook]);
		}
		void* Start = &GVTable[First];
		const SIZE_T Size = (Last - First + 1) * sizeof(void*);

		DWORD OldProtect = 0;
		if (!VirtualProtect(Start, Size, PAGE_READWRITE, &OldProtect))
		{
			UE_LOG(LogTabGroups, Error, TEXT("Could not unprotect the tab well vtable (error %u)."), GetLastError());
			return false;
		}
		for (int32 Hook = 0; Hook < NumHooks; ++Hook)
		{
			GVTable[GSlots[Hook]] = Values[Hook];
		}
		DWORD Ignored = 0;
		VirtualProtect(Start, Size, OldProtect, &Ignored);
		return true;
	}
}

namespace TabWellHook
{
	bool Install(const SDockingTabWell& AnyWell)
	{
		if (GVTable)
		{
			return true;
		}
		if (!FindSlots(GSlots))
		{
			return false;
		}

		GVTable = VTableOf(&AnyWell);
		for (int32 Hook = 0; Hook < NumHooks; ++Hook)
		{
			GOriginals[Hook] = GVTable[GSlots[Hook]];
		}

		void* const Hooks[NumHooks] =
		{
			ToAddress<FArrangeFn>(&FWellHooks::HookArrange),
			ToAddress<FDesiredFn>(&FWellHooks::HookDesired),
			ToAddress<FDropFn>(&FWellHooks::HookDrop),
			ToAddress<FMouseUpFn>(&FWellHooks::HookMouseUp),
			ToAddress<FDragOverFn>(&FWellHooks::HookDragOver),
		};
		if (!WriteSlots(Hooks))
		{
			GVTable = nullptr;
			return false;
		}
		UE_LOG(LogTabGroups, Log, TEXT("Tab well hooked (slots %d, %d, %d, %d, %d)."), GSlots[0], GSlots[1], GSlots[2], GSlots[3], GSlots[4]);
		return true;
	}

	void Uninstall()
	{
		if (GVTable)
		{
			WriteSlots(GOriginals);
			GVTable = nullptr;
		}
	}

	bool IsInstalled()
	{
		return GVTable != nullptr;
	}
}
