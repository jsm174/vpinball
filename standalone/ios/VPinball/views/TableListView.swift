import SwiftUI

enum TableListMode: Int, Hashable {
    case column2
    case column3
    case list
}

struct TableListView: View {
    @ObservedObject var vpinballViewModel = VPinballViewModel.shared
    @ObservedObject var tableManager = TableManager.shared

    var mode: TableListMode
    var sortOrder: SortOrder
    var searchText: String
    @Binding var scrollToTableId: String?

    @State var selectedTable: Table?

    init(mode: TableListMode = .column2, sortOrder: SortOrder = .reverse, searchText: String = "", scrollToTableId: Binding<String?> = .constant(nil)) {
        self.mode = mode
        self.sortOrder = sortOrder
        self.searchText = searchText
        _scrollToTableId = scrollToTableId
    }

    var filteredTables: [Table] {
        return tableManager.filteredTables(searchText: searchText, sortOrder: sortOrder)
    }

    var body: some View {
        ZStack {
            if mode != .list {
                ScrollView {
                    LazyVGrid(columns: Array(repeating: GridItem(.flexible(),
                                                                 alignment: .top),
                                             count: mode == .column2 ? 2 : 3))
                    {
                        ForEach(filteredTables) { table in
                            EmptyView()
                                .id(table.uuid)

                            TableListButton(table: table)
                                .opacity(selectedTable?.uuid == table.uuid ? 0.5 : 1)
                                .onTapGesture {
                                    handlePlay(table)
                                }
                        }
                    }
                    .scrollTargetLayout()
                    .padding(10)
                }
                .scrollPosition(id: $scrollToTableId,
                                anchor: .top)
                .onAppear {
                    // Set refresh control tint to white for visibility on dark background
                    UIRefreshControl.appearance().tintColor = UIColor.white
                }
            } else {
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
                    .onAppear {
                        // Set refresh control tint to white for visibility on dark background
                        UIRefreshControl.appearance().tintColor = UIColor.white
                    }
                    .onChange(of: scrollToTableId) {
                        proxy.scrollTo(scrollToTableId,
                                       anchor: .top)
                    }
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
