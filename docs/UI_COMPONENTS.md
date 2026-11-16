# UI COMPONENTS GUIDE — SETTLESMART 2.0

## 1. Design System Overview

SettleSmart 2.0 adopts a cohesive fintech aesthetic:
- **Backgrounds**: Deep Navy (`#080C14`), Dark Surface (`#111827`), Card Container (`#161F30`).
- **Borders**: Subtle Slate (`#1B2637`, `#233248`).
- **Primary Accent**: Emerald Green (`#10B981`, `#059669`).
- **Secondary Accents**: Teal (`#14B8A6`), Amber (`#F59E0B`), Crimson (`#EF4444`), Purple (`#A855F7`).
- **Typography**: Inter (UI prose), JetBrains Mono (financial amounts, transaction IDs, timestamps).

---

## 2. Common Components

### `Badge`
- **Location**: `src/components/common/Badge.tsx`
- **Usage**: Renders colored pill badges indicating transaction statuses (`Completed`, `Pending`, `Reversed`, `Disputed`), entity statuses (`Active`, `Inactive`), positions (`Creditor`, `Debtor`, `Settled`), or roles (`Admin`, `Finance_Manager`, `Viewer`).

### `FintechCard`
- **Location**: `src/components/common/FintechCard.tsx`
- **Usage**: Standard container card with subtle backdrop blur, border highlights, icon header, and action buttons. Supports an optional `glow` property for emerald drop-shadow effects.

### `Modal`
- **Location**: `src/components/common/Modal.tsx`
- **Usage**: Accessible dialog for forms (Create Entity, Record Transaction, Reversal Confirmation, Add User). Automatically handles ESC key dismissal, click-outside backdrop blur, and width sizing.

### `Drawer`
- **Location**: `src/components/common/Drawer.tsx`
- **Usage**: Slide-over panel from the right edge for inspecting detailed metadata without navigating away from the current view. Used in Entity Directory, Transaction Ledger, and Financial Graph.

### `LoadingSkeleton` & `EmptyState` & `ErrorState`
- **Location**: `src/components/common/`
- **Usage**: Consistent user feedback during async operations, empty table results, or backend network disconnections.

---

## 3. Dashboard Components

### `KPICards`
- **Location**: `src/components/dashboard/KPICards.tsx`
- **Metrics**: Total Entities, Total Transactions, Pending Transactions, Completed Transactions, Total Outstanding Receivables, Total Outstanding Payables.
- **Formatting**: Authoritative paise integers formatted into localized Indian Rupee strings (`₹X,XX,XXX.XX`).

### `ActivityChart`
- **Location**: `src/components/dashboard/ActivityChart.tsx`
- **Technology**: Recharts `AreaChart` with SVG gradients.
- **Interactions**: Interactive tooltip displaying volume and transaction counts; switchable timeframe toggles (Daily, Weekly, Monthly).

### `StatusOverview`
- **Location**: `src/components/dashboard/StatusOverview.tsx`
- **Features**: Visual segmented bar and breakdown list displaying percentage shares of Completed, Pending, Reversed, and Disputed transactions.

### `PriorityQueuePreview`
- **Location**: `src/components/dashboard/PriorityQueuePreview.tsx`
- **Features**: Real-time inspection of the top pending transactions ordered by the backend Max-Heap (Amount desc, Time asc, ID asc). Provides "Process Next" and "Process Batch" controls for authorized roles.

### `RecentTransactionsTable`
- **Location**: `src/components/dashboard/RecentTransactionsTable.tsx`
- **Features**: Quick ledger overview with sender/receiver flow, status badges, and instant click-to-view detail drawers.

---

## 4. Graph Visualization Components

### `FinancialGraphCanvas`
- **Location**: `src/components/graph/FinancialGraphCanvas.tsx`
- **Technology**: Custom interactive SVG canvas.
- **Capabilities**:
  - **Pan & Zoom**: Infinite 2D panning with mouse drag and zoom in/out from 0.4x to 2.5x with reset button.
  - **Node Dragging**: Draggable entity nodes allowing users to customize topology on the fly.
  - **Directed Quadratic Bezier Edges**: Arrows indicating direction of money flow. Parallel transactions between the same pair of entities receive distinct curvature offsets (`Q midX midY`), ensuring no two transactions overlap.
  - **Reversal Rendering**: Reversal transactions appear as distinct reverse-direction edges with crimson arrowheads.
  - **Status Coloring**: Completed (Emerald), Pending (Dashed Amber), Reversed (Red).
  - **Search & Filter**: Search entity by name/ID to center and highlight; filter edges by status (`Completed`, `Pending`, `Reversed`).
  - **Live WebSocket Pulse**: Flashes pulsating highlight when an incoming transaction event is received from the outbox.

---

## 5. Settlement Flow Components

### `SettlementFlowChart`
- **Location**: `src/components/settlement/SettlementFlowChart.tsx`
- **Features**: Directional visual cards showing paying entity (debtor), receiving entity (creditor), and recommended amount. Displays prominent advisory disclaimer emphasizing that recommendations represent mathematical optimizations rather than completed transactions.
