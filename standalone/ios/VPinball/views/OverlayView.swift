import SwiftUI

struct OverlayView: View {
    @EnvironmentObject var vpinballViewModel: VPinballViewModel

    var body: some View {
        ZStack {
        }
    }
}

#Preview {
    let vpinballViewModel = VPinballViewModel.shared

    ZStack {
        Color.gray.ignoresSafeArea()

        OverlayView()
    }
    .environmentObject(vpinballViewModel)
}
