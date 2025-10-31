import SwiftUI

enum TableListMode: Int, Hashable { case small, medium, large, list }

struct TableListView: View {
    @ObservedObject var vpinballViewModel = VPinballViewModel.shared
    @ObservedObject var tableManager = TableManager.shared

    var mode: TableListMode
    var sortOrder: SortOrder
    var searchText: String
    @Binding var scrollToTable: Table?

    @State var selectedTable: Table?

    init(mode: TableListMode = .medium, sortOrder: SortOrder = .reverse, searchText: String = "", scrollToTable: Binding<Table?> = .constant(nil)) {
        self.mode = mode
        self.sortOrder = sortOrder
        self.searchText = searchText
        _scrollToTable = scrollToTable
    }

    var filteredTables: [Table] {
        return tableManager.filteredTables(searchText: searchText, sortOrder: sortOrder)
    }

    var body: some View {
        ZStack {
            if mode == .list {
                ScrollViewReader { proxy in
                    List {
                        ForEach(filteredTables) { table in
                            Button {
                                handlePlay(table)
                            }
                            label: {
                                HStack(spacing: 10) {
                                    TableListButton(table: table,
                                                    showTitle: false)
                                        .frame(height: 120)

                                    Text(table.name)
                                        .frame(maxWidth: .infinity,
                                               alignment: .leading)
                                        .multilineTextAlignment(.leading)
                                        .foregroundStyle(Color.white)
                                }
                            }
                            .id(table.uuid)
                            .swipeActions(edge: .trailing,
                                          allowsFullSwipe: false)
                            {
                                Button {
                                    withAnimation(nil) {
                                        handleDelete(table: table)
                                    }
                                } label: {
                                    Image(uiImage: UIImage(systemName: "trash")!.withTintColor(.white,
                                                                                               renderingMode: .alwaysOriginal))
                                    Text("Delete")
                                }
                                .tint(Color.vpxRed)
                            }
                            .listRowInsets(.init(top: 10,
                                                 leading: 10,
                                                 bottom: 10,
                                                 trailing: 10))
                            .listRowSeparatorTint(Color.darkGray)
                            .listRowBackground(selectedTable?.uuid == table.uuid ? Color.darkGray : Color.lightBlack)
                            .alignmentGuide(.listRowSeparatorLeading) { _ in
                                0
                            }
                        }
                    }
                    .listStyle(.plain)
                    .onChange(of: scrollToTable) {
                        if let table = scrollToTable {
                            proxy.scrollTo(table.uuid, anchor: .top)
                        }
                    }
                }
            } else {
                GeometryReader { geo in
                    let size = geo.size
                    let layout = computeColumns(containerW: size.width - 32, // horizontal padding 16+16
                                                availableH: size.height,
                                                mode: mode)
                    ScrollView {
                        LazyVGrid(columns: Array(repeating: GridItem(.fixed(layout.cardW), spacing: layout.gap, alignment: .top), count: layout.cols), spacing: layout.gap) {
                            ForEach(filteredTables) { table in
                                EmptyView().id(table.uuid)
                                TableListButton(table: table)
                                    .opacity(selectedTable?.uuid == table.uuid ? 0.5 : 1)
                                    .onTapGesture { handlePlay(table) }
                                    .frame(width: layout.cardW, height: layout.cardW * 1.5)
                            }
                        }
                        .scrollTargetLayout()
                        .padding(.horizontal, 16)
                        .padding(.vertical, 16)
                    }
                    .scrollPosition(id: Binding(
                        get: { scrollToTable?.uuid },
                        set: { _ in scrollToTable = nil }
                    ), anchor: .top)
                }
            }

            if filteredTables.isEmpty && !searchText.isEmpty {
                GeometryReader { geometry in
                    VStack {
                        Spacer()

                        VStack(spacing: 40) {
                            TablePlaceholderImage()

                            VStack(spacing: 20) {
                                Text("Shoot Again!")
                                    .font(.title)
                                    .bold()
                                    .foregroundStyle(Color.vpxDarkYellow)
                                    .blinkEffect()

                                Text("Please make sure the table name is correct, or try searching for another table.")
                                    .font(.callout)
                                    .multilineTextAlignment(.center)
                                    .foregroundStyle(Color.white)
                                    .padding(.horizontal, 40)
                            }
                        }
                        .frame(height: geometry.size.height * 0.80)

                        Spacer()
                    }
                    .frame(maxWidth: .infinity)
                }
            }
        }
    }

    func handleDelete(table: Table) {
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) {
            vpinballViewModel.setAction(action: .delete,
                                        table: table)
        }
    }

    func handlePlay(_ table: Table) {
        selectedTable = table

        DispatchQueue.main.asyncAfter(deadline: .now() + 0.25) {
            selectedTable = nil

            DispatchQueue.main.asyncAfter(deadline: .now() + 0.25) {
                vpinballViewModel.setAction(action: .play,
                                            table: table)
            }
        }
    }
}

// MARK: - Responsive Grid Logic (Small/Medium/Large)
extension TableListView {
    private var baseGap: CGFloat { 12 }
    private var ratio: CGFloat { 2/3 }
    private var minReadableW: CGFloat { 120 }
    private var minFloorRegular: CGFloat { 48 }
    private var minFloorCompact: CGFloat { 40 }

    private func heightFactor(_ mode: TableListMode) -> CGFloat {
        switch mode {
        case .small: return 0.72
        case .medium: return 0.88
        case .large: return 1.0
        case .list: return 1.0
        }
    }

    private func computeTiers(containerW: CGFloat, availableH: CGFloat, gap: CGFloat, minFloor: CGFloat) -> (small: Int, medium: Int, large: Int, maxWFromH: CGFloat) {
        let baseCap = max(60, availableH) * ratio
        func kFor(capFactor: CGFloat) -> Int {
            var minW = minReadableW
            let effCap = baseCap * capFactor
            var effMin = min(minW, effCap)
            var K = Int(floor((containerW + gap) / (effMin + gap)))
            while K < 3 && minW > minFloor {
                minW -= 6
                effMin = min(minW, effCap)
                K = Int(floor((containerW + gap) / (effMin + gap)))
            }
            return max(1, K)
        }
        let kSmallRaw = kFor(capFactor: 0.72)
        let kMedRaw   = kFor(capFactor: 0.88)
        let kLargeRaw = kFor(capFactor: 1.00)

        var small = max(3, kSmallRaw)
        var medium = min(kMedRaw, small - 1)
        if medium < 2 { medium = max(2, small - 1) }
        var large  = min(kLargeRaw, medium - 1)
        if large < 1 { large = 1 }

        return (small, medium, large, baseCap)
    }

    private func cardWidthForCols(_ k: Int, containerW: CGFloat, cap: CGFloat, mode: TableListMode, gap: CGFloat) -> CGFloat {
        let perCol = (containerW - gap * CGFloat(max(k - 1, 0))) / CGFloat(max(k, 1))
        var w = min(perCol, cap)
        if k == 1 {
            let factor: CGFloat = {
                switch mode {
                case .small:  return 0.86
                case .medium: return 0.94
                case .large:  return 1.00
                case .list:   return 0.94
                }
            }()
            w = min(w, containerW * factor)
        }
        return floor(w)
    }

    private func computeColumns(containerW: CGFloat, availableH: CGFloat, mode: TableListMode) -> (cols: Int, cardW: CGFloat, gap: CGFloat) {
        let layout: (gap: CGFloat, minFloor: CGFloat) = (availableH < 420) ? (8, minFloorCompact) : (baseGap, minFloorRegular)
        let tiers = computeTiers(containerW: containerW, availableH: availableH, gap: layout.gap, minFloor: layout.minFloor)
        // Enforce ordering across modes with cap: Small <= 7, Medium < Small, Large < Medium
        let effSmall = min(tiers.small, 6)
        var effMedium = min(tiers.medium, effSmall - 1)
        if effMedium < 2 { effMedium = max(1, effSmall - 1) }
        var effLarge = min(tiers.large, effMedium - 1)
        if effLarge < 1 { effLarge = 1 }
        let cols: Int = {
            switch mode {
            case .small:  return effSmall
            case .medium: return effMedium
            case .large:  return effLarge
            case .list:   return effMedium
            }
        }()
        let cap = max(60, availableH) * ratio * heightFactor(mode)
        let w = cardWidthForCols(cols, containerW: containerW, cap: cap, mode: mode, gap: layout.gap)
        return (cols, w, layout.gap)
    }
}
