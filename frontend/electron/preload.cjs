const { contextBridge, ipcRenderer } = require("electron");

contextBridge.exposeInMainWorld("archivist", {
  selectFolder: () => ipcRenderer.invoke("dialog:selectFolder"),
});
