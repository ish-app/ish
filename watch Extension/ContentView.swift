//
//  ContentView.swift
//  watch Extension
//
//  Created by Alex Gustafsson on 2020-07-26.
//

import SwiftUI

struct ContentView: View {
    @State var showingDetail = false
    @ObservedObject var detail = Detail()

    var body: some View {
        List {
            Button(action: {
                self.detail.name = "Google DNS"
                self.showingDetail.toggle()
            }) {
                Text("Google DNS")
            }.sheet(isPresented: $showingDetail) {
                DetailView(name: self.$detail.name)
            }
            
            Button(action: {
                self.detail.name = "Ping servers"
                self.showingDetail.toggle()
            }) {
                Text("Ping servers")
            }.sheet(isPresented: $showingDetail) {
                DetailView(name: self.$detail.name)
            }
        }
        .navigationBarTitle("iSH")
    }
}

struct ContentView_Previews: PreviewProvider {
    static var previews: some View {
        ContentView()
    }
}
