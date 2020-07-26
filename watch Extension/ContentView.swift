//
//  ContentView.swift
//  watch Extension
//
//  Created by Alex Gustafsson on 2020-07-26.
//

import SwiftUI
import WatchConnectivity
import WatchConnectivity

struct ContentView: View {
    @State var showingDetail = false
    @ObservedObject var detail = Detail()
    
    @State var showingError = false
    @State var errorMessage: String = ""
    @State var error: Error? = nil

    var body: some View {
        List {
            Button(action: {
                self.detail.name = "Google DNS"
                self.showingDetail.toggle()

                if WCSession.isSupported() {
                    if WCSession.default.isReachable {
                        WCSession.default.sendMessage(["command": "ls -l\n"], replyHandler: { reply -> Void in
                            print("Got reply: \(reply)")
                        }, errorHandler: { (error) -> Void in
                            self.errorMessage = "Sending message failed"
                            self.error = error
                            self.showingDetail = false
                            self.showingError = true
                        })
                    } else {
                        self.errorMessage = "Unable to communicate with iSH - not reachable"
                        self.error = nil
                        self.showingDetail = false
                        self.showingError = true
                    }
                } else {
                    self.errorMessage = "WatchConnectivity is not supported"
                    self.error = nil
                    self.showingDetail = false
                    self.showingError = true
                }
            }) {
                Text("Google DNS")
            }.sheet(isPresented: $showingDetail) {
                DetailView(name: self.$detail.name)
            }.sheet(isPresented: $showingError) {
                ErrorView(message: self.errorMessage, error: self.error)
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
