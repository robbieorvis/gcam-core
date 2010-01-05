package ModelInterface.ModelGUI2;

import java.awt.BorderLayout;
import java.awt.Cursor;
import java.awt.Dimension;
import java.awt.Frame;
import java.awt.HeadlessException;
import java.awt.Window;
import java.awt.event.ComponentEvent;
import java.awt.event.ComponentListener;
import java.awt.event.MouseAdapter;
import java.awt.event.WindowAdapter;
import java.awt.event.WindowListener;
import java.awt.event.WindowEvent;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.Iterator;
import java.util.Random;
import java.util.Vector;
import java.util.List;

import javax.swing.BorderFactory;
import javax.swing.Box;
import javax.swing.BoxLayout;
import javax.swing.JCheckBox;
import javax.swing.JDialog;
import javax.swing.JFrame;
import javax.swing.JLabel;
import javax.swing.JOptionPane;
import javax.swing.JPanel;
import javax.swing.JProgressBar;
import javax.swing.SwingUtilities;
import javax.swing.JButton;
import javax.swing.event.ChangeListener;
import java.awt.event.ActionListener;
import java.awt.event.ActionEvent;

import org.apache.poi.hssf.usermodel.HSSFPatriarch;
import org.apache.poi.hssf.usermodel.HSSFSheet;
import org.apache.poi.hssf.usermodel.HSSFWorkbook;
import org.apache.poi.hssf.usermodel.HSSFRow;
import org.w3c.dom.Element;
import org.w3c.dom.Node;
import org.w3c.dom.NodeList;
import org.w3c.dom.xpath.XPathResult;

import com.sleepycat.dbxml.XmlException;
import com.sleepycat.dbxml.XmlQueryContext;

import ModelInterface.ModelGUI2.queries.QueryGenerator;
import ModelInterface.ModelGUI2.tables.ComboTableModel;
import ModelInterface.ModelGUI2.tables.MultiTableModel;
import ModelInterface.ModelGUI2.xmldb.XMLDB;



// TODO: Auto-generated Javadoc
/**
 * The Class batchWindow. This class creates a new window with a progress 
 * bar when a batch query is run. If this window is exited before it is 
 * finished collecting the results, then the query is canceled.  
 */
public class BatchWindow extends Window {




	/**
	 * 
	 */
	private static final long serialVersionUID = -1481157258336281895L;

	final File excelFile;
	final Vector<Object[]> toRunScns; 
	final JCheckBox singleSheetCheckBox; 
	final JCheckBox drawPicsCheckBox; 
	final int numQueries; 
	final XPathResult res;
	final JFrame parentFrame;
	Thread exportThread;
	XmlQueryContext context	= null;
	final JCheckBox overwriteCheckBox;
	final boolean overwrite;
	final JProgressBar progressBar;
	final Runnable increaseProgress;
	final Window progressDialog;


	/**
	 * Instantiates a new batch window. A window with a progress bar is made. 
	 * The window terminates the operating thread upon exiting the window.  
	 * 
	 * @param excelFile file that the excel sheet will be saved in
	 * @param toRunScns the scans to run
	 * @param singleSheetCheckBox check box corresponding to single or multiple sheets
	 * @param drawPicsCheckBox option to draw charts
	 * @param numQueries the number of queries
	 * @param res The XPath results which will contain the aQuery to run.
	 * @param parentFrame the parent frame GUI
	 * @param overwriteCheckBox option to overwrite existing file
	 */
	public BatchWindow(final File excelFile, final Vector<Object[]> toRunScns,
			final JCheckBox singleSheetCheckBox, final JCheckBox drawPicsCheckBox,
			final int numQueries, final XPathResult res, final JFrame parentFrame,
			JCheckBox overwriteCheckBox) {



		super(parentFrame);
		this.excelFile = excelFile;
		this.toRunScns = toRunScns;
		this.singleSheetCheckBox = singleSheetCheckBox;
		this.drawPicsCheckBox = drawPicsCheckBox;
		this.numQueries = numQueries;
		this.res = res;
		this.parentFrame = parentFrame;
		this.overwriteCheckBox = overwriteCheckBox;
		this.overwrite = overwriteCheckBox.isSelected();


		//used as a lock
		context = XMLDB.getInstance().createQueryContext();

		progressBar = new JProgressBar(0, numQueries*toRunScns.size());
		// TODO: createProgressBarGUI should be moved somewhere else

		// the createProgressBarGUI sets it visible
		increaseProgress = new Runnable() {
			public void run() {
				progressBar.setValue(progressBar.getValue()+1);
			}
		};

		//This method was copied from XMLDB and edited for this application
		progressDialog = createProgressBarGUI2(parentFrame, progressBar, 
				"Running Queries", "Run and Export Progress");
		WindowAdapter myWindowAdapter = new WindowAdapter() {
			public void windowClosing(WindowEvent e) {
				killThread();
			}
		};
		progressDialog.addWindowListener(myWindowAdapter);
		progressDialog.addWindowStateListener(myWindowAdapter);

		//Thread that compiles the data. Everything visual has been 
		//completed before this point, except updating the progress bar.
		exportThread = new Thread(){

			public void run() {
				Node tempNode;
				HSSFWorkbook wb = null;
				HSSFSheet sheet = null;
				HSSFPatriarch drawingPat = null;
				QueryGenerator qgTemp = null;
				List<String>tempRegions = new Vector<String>();
				int numErrors = 0;
				// read/create the output excel file


				if(isInterrupted())
					return;

				if(excelFile.exists() && !(overwrite)) {
					try {
						wb = new HSSFWorkbook(new FileInputStream(excelFile));
					} catch (IOException ioe) {
						ioe.printStackTrace();
						return;
					}
				}
				//Option to add results to an existing file has been added.
				if(wb == null || (excelFile.exists() && (overwrite))) {
					wb = new HSSFWorkbook();
					for(int i = 1; i < wb.getNumberOfSheets(); i++)
						wb.removeSheetAt(i);
				}
				for(Iterator<Object[]> itScn = toRunScns.iterator(); itScn.hasNext(); ) {
					Object[] currScns = itScn.next();
					if(!singleSheetCheckBox.isSelected()) { 
						sheet = wb.createSheet("Sheet"+String.valueOf(wb.getNumberOfSheets()+1));
						drawingPat = drawPicsCheckBox.isSelected() ? sheet.createDrawingPatriarch() : null;
					}
					for(int snapshotIndex = 0; snapshotIndex < numQueries; ++snapshotIndex) {
						tempNode = res.snapshotItem(snapshotIndex);
						tempRegions.clear();
						NodeList nl = tempNode.getChildNodes();
						boolean isGlobal = false;
						for(int i = 0; i < nl.getLength(); ++i) {
							Node currEl = nl.item(i);
							if(currEl.getNodeName().equals("region")) {
								String currRegionName = ((Element)currEl).getAttribute("name");
								// if Global is in the list we will run it separately
								if(!currRegionName.equals("Global")) {
									tempRegions.add(((Element)currEl).getAttribute("name"));
								} else {
									isGlobal = true;
								}
							} else {
								try {
									qgTemp = new QueryGenerator(currEl);
								} catch (NullPointerException e) {
									e.printStackTrace();
									// don�t warn the user just yet
								} catch(ClassCastException ce) {
									ce.printStackTrace();
									// don't want the user yet since this may have just
									// been a comment mistaken as the query
									// TODO: we need a better way of knowing if something
									// is a query
								}
							}
						}
						if(qgTemp == null) {
							JOptionPane.showMessageDialog(parentFrame,
									"There was an error while trying to create query." +
									"<html><br></html>" + "Check xml code.",
									"Batch Query Error", JOptionPane.ERROR_MESSAGE);
							
							++numErrors;
							continue;
						}
						if(singleSheetCheckBox.isSelected()) {
							sheet = wb.createSheet("Sheet"+String.valueOf(wb.getNumberOfSheets()+1));
							drawingPat = drawPicsCheckBox.isSelected() ? sheet.createDrawingPatriarch() : null;
						}

						if(isInterrupted())
							return;

						try {
							if(tempRegions.size() > 0) {
								if(qgTemp.isGroup()) {
									(new MultiTableModel(qgTemp, currScns, 
											tempRegions.toArray(), 
											parentFrame, context)).exportToExcel(sheet, wb, drawingPat);
								} else {
									(new ComboTableModel(qgTemp, currScns, 
											tempRegions.toArray(), 
											parentFrame, null,context)).exportToExcel(sheet, wb, drawingPat);
								}
							}
							// if global was selected we will run it again.  this covers the case where
							// Global and other regions where selected
							if(isGlobal) {
								tempRegions.clear();
								tempRegions.add("Global");
								if(qgTemp.isGroup()) {
									(new MultiTableModel(qgTemp, currScns, 
											tempRegions.toArray(), 
											parentFrame,context)).exportToExcel(sheet, wb, drawingPat);
								} else {
									(new ComboTableModel(qgTemp, currScns, 
											tempRegions.toArray(), 
											parentFrame, null,context)).exportToExcel(sheet, wb, drawingPat);
								}
							}
						} catch(Exception e) {
							e.printStackTrace();
							HSSFRow row = sheet.createRow(sheet.getLastRowNum()+1);
							row.createCell((short)0).setCellValue("Error: "+e.getMessage());
							++numErrors;
						} finally {
							SwingUtilities.invokeLater(increaseProgress);
						}
					}

				}
				if(isInterrupted())
					return;


				try {
					String fileName = excelFile.toString();
					if(fileName.contains(".xml"))
						fileName = fileName.replace(".xml", "");
					if(fileName.contains(".xls"))
						fileName = fileName.replace(".xls", "");

					FileOutputStream fos = new FileOutputStream(fileName+".xls");
					wb.write(fos);
					fos.close();
					if(numErrors == 0) {
						JOptionPane.showMessageDialog(parentFrame,
								"Sucessfully ran batch query",
								"Batch Query", JOptionPane.INFORMATION_MESSAGE);
					} else {
						// warn the users that some queries had errors
						JOptionPane.showMessageDialog(parentFrame,
								"Batch queries finished with "+numErrors+" error"+(numErrors == 1 ? "." : "s."),
								"Batch Query", JOptionPane.WARNING_MESSAGE);
					}
				} catch(IOException ioe) {
					ioe.printStackTrace();
					JOptionPane.showMessageDialog(parentFrame,
							"There was an error while trying to write results",
							"Batch Query Error", JOptionPane.ERROR_MESSAGE);
				} finally {
					progressDialog.dispose();
				}
			}
		};

		exportThread.start();
	}


	/**
	 * Creates the progress bar GUI. 
	 * 
	 * @param parentFrame the parent frame GUI
	 * @param progBar the progress bar
	 * @param title title of the window
	 * @param labelStr the label of the window
	 * 
	 * @return the window
	 */
	public static Window createProgressBarGUI2(Frame parentFrame, JProgressBar progBar, String title, 
			String labelStr) {
		if(progBar.getMaximum() == 0) {
			return null;
		}
		final JDialog filterDialog = new JDialog(parentFrame, title, false);
		filterDialog.setResizable(false);
		filterDialog.setAlwaysOnTop(true);
		JPanel all = new JPanel();
		all.setLayout( new BoxLayout(all, BoxLayout.Y_AXIS));
		progBar.setPreferredSize(new Dimension(200, 20));
		JLabel label = new JLabel(labelStr);

		all.add(label, BorderLayout.PAGE_START);
		all.add(Box.createVerticalStrut(10));
		all.add(progBar);
		final JButton cancelButton = new JButton("Cancel");
		cancelButton.addActionListener(new ActionListener() {
			public void actionPerformed(ActionEvent e) {
				// let listeners know the window is closing
				final WindowEvent windowEvent = new WindowEvent(filterDialog, WindowEvent.WINDOW_CLOSING);
				for(WindowListener adapter : filterDialog.getWindowListeners()) {
					adapter.windowClosing(windowEvent);
				}
				// close the window
				filterDialog.dispose();
			}
		});
		final JPanel buttonPanel = new JPanel();
		buttonPanel.setLayout(new BoxLayout(buttonPanel, BoxLayout.X_AXIS));
		buttonPanel.add(Box.createHorizontalGlue());
		buttonPanel.add(cancelButton);
		all.add(Box.createVerticalStrut(10));
		all.add(buttonPanel);
		all.setBorder(BorderFactory.createEmptyBorder(20, 20, 20, 20));


		filterDialog.add(all, BorderLayout.PAGE_START);
		filterDialog.pack();
		filterDialog.setVisible(true);
		return filterDialog;
	}



	/**
	 * Kill the query thread. 
	 * @throws IOException 
	 */
	public void killThread() {
		try {
			context.interruptQuery();
		} catch (XmlException e) {
			e.printStackTrace();
		} finally {
			context.delete();
		}
		exportThread.interrupt();
		System.out.println("Tried to kill it");
	}

}