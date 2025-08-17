import SwiftUI

struct CustomTableOptionView: View {
    @Binding var customTableOption: CustomTableOption
    @State var label: String = ""
    @State var options: [String] = []
    @State var value: Float = 0.0
    @State var isUpdating: Bool = false

    let vpinballManager = VPinballManager.shared

    var body: some View {
        VStack(alignment: .leading,
               spacing: 4)
        {
            if !options.isEmpty {
                HStack {
                    Text(label)
                        .bold()
                        .foregroundStyle(Color.white)
                        .padding(.leading, 10)

                    Spacer()

                    Picker("", selection: $value) {
                        ForEach(options.indices, id: \.self) { index in
                            let option = options[index]
                            Text(option.capitalized)
                                .tag(Float(index))
                        }
                    }
                    .tint(Color.white)
                }
            } else {
                Text(label)
                    .bold()
                    .foregroundStyle(Color.white)
                    .padding(.leading, 10)

                HStack {
                    Slider(
                        value: Binding(
                            get: { Double(value) },
                            set: { newValue in
                                let roundedValue = round(newValue * 1000) / 1000
                                if abs(roundedValue - Double(customTableOption.maxValue)) < Double(customTableOption.step) {
                                    value = customTableOption.maxValue
                                } else {
                                    value = Float(roundedValue)
                                }
                            }
                        ),
                        in: Double(customTableOption.minValue) ... Double(customTableOption.maxValue),
                        step: Double(customTableOption.step)
                    )

                    if let optionUnit = VPinballOptionUnit(rawValue: CInt(customTableOption.unit)) {
                        Text(optionUnit.formatValue(value))
                            .foregroundStyle(Color.white)
                            .frame(width: 75,
                                   alignment: .trailing)
                    }
                }
                .padding(.horizontal, 10)
            }
        }
        .padding(.top, 10)
        .padding(.bottom, 10)
        .onAppear {
            handleRefresh()
        }
        .onChange(of: value) {
            handleUpdate()
        }
    }

    func handleRefresh() {
        isUpdating = true

        label = customTableOption.name

        options = customTableOption.literals.split(separator: "||").map { String($0) }

        value = customTableOption.value

        DispatchQueue.main.asyncAfter(deadline: .now() + 0.01) {
            isUpdating = false
        }
    }

    func handleUpdate() {
        if isUpdating {
            return
        }

        let updatedOption = CustomTableOption(
            sectionName: customTableOption.sectionName,
            id: customTableOption.id,
            name: customTableOption.name,
            showMask: customTableOption.showMask,
            minValue: customTableOption.minValue,
            maxValue: customTableOption.maxValue,
            step: customTableOption.step,
            defaultValue: customTableOption.defaultValue,
            unit: customTableOption.unit,
            literals: customTableOption.literals,
            value: value
        )

        customTableOption = updatedOption
        vpinballManager.setCustomTableOption(updatedOption)
    }
}
