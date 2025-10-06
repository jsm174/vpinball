import SwiftUI

class VPinballViewModel: ObservableObject {
    static let shared = VPinballViewModel()

    enum Action {
        case play
        case stopped
        case rename
        case changeTableImage
        case viewScript
        case share
        case reset
        case delete
        case showError
    }

    @Published var didSetAction: UUID?
    @Published var showHUD = false
    @Published var isPlaying = false
    @Published var tableImage: UIImage?
    @Published var webServerURL: String?
    @Published var scrollToTableId: String?
    @Published var hudTitle: String?
    @Published var hudProgress: Int = 0
    @Published var hudStatus: String?

    var action: Action?
    var table: Table?
    var scriptError: String?

    func setAction(action: Action, table: Table? = nil) {
        self.action = action
        self.table = table

        didSetAction = UUID()
    }

    func showProgressHUD(title: String, status: String) {
        hudTitle = title
        hudProgress = 0
        hudStatus = status
        showHUD = true
    }

    func updateProgressHUD(progress: Int, status: String) {
        hudProgress = progress
        hudStatus = status
    }

    func updateProgressHUD(progress: Int) {
        hudProgress = progress
    }

    func hideHUD() {
        showHUD = false
    }
}
