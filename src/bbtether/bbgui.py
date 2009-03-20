from bb_gui import BBGui
import bb_messenging
# Main
bb_messenging.confirm(["This is not ready or tested yet !"])
gui = BBGui(0)
# enable feddback from bbtether to gui
bb_messenging.gui = gui
gui.MainLoop()
