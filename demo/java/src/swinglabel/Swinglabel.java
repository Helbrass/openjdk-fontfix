package swinglabel;

import java.awt.Color;
import java.awt.Font;
import javax.swing.BorderFactory;
import javax.swing.Box;
import javax.swing.BoxLayout;
import javax.swing.JFrame;
import javax.swing.JLabel;
import javax.swing.JPanel;
import javax.swing.SwingUtilities;

public class Swinglabel implements Runnable {

    public static void main(String[] args) {
        System.setProperty("swing.boldMetal", "false");
        SwingUtilities.invokeLater(new Swinglabel());
    }

    private JPanel createContentPane() {
        JPanel panel = new JPanel();
        panel.setLayout(new BoxLayout(panel, BoxLayout.Y_AXIS));
        panel.setBackground(Color.WHITE);
        panel.setBorder(BorderFactory.createEmptyBorder(12, 12, 12, 12));

        for (int a = 6; a <= 18; a += 2) {
            panel.add(createLabel("DejaVu Sans Mono", a));
        }

        panel.add(Box.createVerticalStrut(20));

        for (int a = 18; a >= 6; a -= 2) {
            panel.add(createLabel("Consolas", a));
        }

        panel.add(Box.createVerticalStrut(20));

        for (int a = 6; a <= 18; a += 2) {
            panel.add(createLabel(Font.MONOSPACED, a));
        }

        panel.add(Box.createVerticalStrut(20));

        for (int a = 18; a >= 6; a -= 2) {
            panel.add(createLabel(Font.SANS_SERIF, a));
        }

        return panel;
    }

    private JLabel createLabel(String fontFamily, int fontSize) {
        JLabel label = new JLabel("The quick brown fox jumps over the lazy dog (" + fontFamily + (fontSize == 0 ? ")" : " " + fontSize + ")"));
        label.setForeground(Color.BLACK);
        Font font = new Font(fontFamily, Font.PLAIN, fontSize == 0 ? label.getFont().getSize() : fontSize);
        label.setFont(font);
        return label;
    }

    @Override
    public void run() {
        JFrame frame = new JFrame("java-font-demo");
        frame.setContentPane(createContentPane());
        frame.setBackground(Color.WHITE);
        frame.setDefaultCloseOperation(JFrame.DISPOSE_ON_CLOSE);
        frame.pack();
        frame.setLocationRelativeTo(null);
        frame.setVisible(true);
    }
}
