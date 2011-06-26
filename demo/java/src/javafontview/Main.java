package javafontview;

import java.awt.BorderLayout;
import java.awt.Color;
import java.awt.Font;
import java.io.File;
import javax.swing.BorderFactory;
import javax.swing.JFrame;
import javax.swing.JLabel;
import javax.swing.JPanel;
import javax.swing.SwingUtilities;

public class Main implements Runnable {

    private static final String ARGUMENT_SIZE = "--size";
    private static final String ARGUMENT_FILE = "--file";

    @SuppressWarnings("CallToThreadDumpStack")
    public static void main(String[] args) {
        System.setProperty("swing.boldMetal", "false");
        printUsage();

        int size = 10;
        String file = "/usr/share/fonts/truetype/DejaVuSansMono.ttf";

        if (args != null && args.length > 0 && args.length % 2 == 0) {
            for (int i = 0; i < args.length; i++) {
                if (ARGUMENT_SIZE.equals(args[i])) {
                    i++;
                    String sizeString = args[i];
                    try {
                        size = Integer.parseInt(sizeString);
                    } catch (Exception ex) {
                        ex.printStackTrace();
                    }
                    continue;
                }
                if (ARGUMENT_FILE.equals(args[i])) {
                    i++;
                    file = args[i];
                }
            }
        }
        SwingUtilities.invokeLater(new Main(file, size));
    }

    private static void printUsage() {
        System.out.println(""
                + "Java (swing) font viewer demo.\n"
                + "By default shown font is DejaVu Sans Mono size 10.\n"
                + "usage: --file <path to TTF file to read font from> --size <font size>");
    }

    public Main(String file, int size) {
        this.file = file;
        this.size = size;
    }
    /** font file to read font from */
    private final String file;
    /** font size to use for label */
    private final int size;

    @Override
    @SuppressWarnings("CallToThreadDumpStack")
    public void run() {
        try {
            Font font = Font.createFont(Font.TRUETYPE_FONT, new File(file));
            font = font.deriveFont((float) size);
            JLabel label = new JLabel("The quick brown fox jumps over the lazy dog");
            label.setForeground(Color.BLACK);

            JPanel panel = new JPanel(new BorderLayout());
            panel.setBackground(Color.WHITE);
            panel.setBorder(BorderFactory.createEmptyBorder(20, 20, 20, 20));
            panel.add(label, BorderLayout.CENTER);

            JFrame frame = new JFrame("fontview java");
            frame.setDefaultCloseOperation(JFrame.DISPOSE_ON_CLOSE);
            frame.setContentPane(panel);
            frame.setLocation(0, 0);
            frame.pack();
            frame.setVisible(true);
        } catch (Exception e) {
            e.printStackTrace();
        }
    }
}
