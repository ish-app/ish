//
//  DetailView.swift
//  watch Extension
//
//  Created by Alex Gustafsson on 2020-07-26.
//

import SwiftUI

class Detail: ObservableObject {
    @Published var name: String = ""
}

struct DetailView: View {
    @Binding var name: String

    var body: some View {
        ScrollView(.vertical) {
            VStack {
                Text(name)
                    .multilineTextAlignment(.leading)
                    .frame(minWidth: 0, maxWidth: .infinity, alignment: .topLeading)
                Divider()
                VStack {
                    Text("$ output from iSH displayed here")
                        .frame(minWidth: 0, maxWidth: .infinity, alignment: .topLeading)
                    ForEach(0 ..< 100) { number in
                        Text("line by line")
                        .padding(.top, 5.0)
                        .frame(minWidth: 0, maxWidth: .infinity, alignment: .topLeading)
                    }
                }
                .background(/*@START_MENU_TOKEN@*/Color.white/*@END_MENU_TOKEN@*/)
                .frame(minWidth: 0, maxWidth: .infinity, alignment: .topLeading)
                .foregroundColor(Color.black)
                .font(Font.body)
            }
        }
    }
}

struct DetailView_Previews: PreviewProvider {
    static var previews: some View {
        DetailView(name: .constant("Example action"))
    }
}
